/*
 * Copyright (c) 2014 Patrick Kelsey. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_passiveinet.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/if_promiscinet.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_promisc.h>
#include <net/pfil.h>

#include "uinet_api.h"
#include "uinet_config_internal.h"
#include "uinet_host_interface.h"

#include "opt_inet6.h"


int
uinet_inet6_enabled(void)
{
#ifdef INET6
	return (1);
#else
	return (0);
#endif
}


int
uinet_initialize_thread(void)
{
	struct uinet_thread *utd;
	struct thread *td;
	int cpuid;

	/*
	 * uinet_shutdown() waits for a message from the shutdown thread
	 * indicating shutdown is complete.  If uinet_shutdown() is called
	 * from a signal handler running in a thread context that is holding
	 * a lock that the shutdown activity needs to acquire in order to
	 * complete, deadlock will occur.  Masking all signals in all
	 * threads that use the uinet API prevents such a deadlock by
	 * preventing all signal handlers (and thus any that might call
	 * uinet_shutdown()) from running in the context of any thread that
	 * might be holding a lock required by the shutdown thread.
	 */
	uhi_mask_all_signals();

	utd = uhi_thread_get_thread_specific_data();
	if (NULL == utd) {
		utd = uinet_thread_alloc(NULL);
		if (NULL == utd)
			return (ENOMEM);
		
		td = utd->td;

		KASSERT(sizeof(td->td_wchan) >= sizeof(uhi_thread_t), ("uinet_initialize_thread: can't safely store host thread id"));
		td->td_wchan = (void *)uhi_thread_self();

		uhi_thread_set_thread_specific_data(utd);
	} else {
		td = utd->td;
	}

	cpuid = uhi_thread_bound_cpu();
	td->td_oncpu = (cpuid == -1) ? 0 : cpuid;

	return (0);
}


void
uinet_finalize_thread(void)
{
	struct uinet_thread *utd;

	utd = uhi_thread_get_thread_specific_data();

	if (utd != NULL) {
		uinet_thread_free(utd);
		uhi_thread_set_thread_specific_data(NULL);
	}
}


int
uinet_getifstat(const char *name, struct uinet_ifstat *stat)
{
	struct uinet_config_if *ifcfg;
	struct ifnet *ifp;

	ifcfg = uinet_iffind_byname(name);
	if (NULL == ifcfg) {
		printf("could not find interface %s\n", name);
		return (EINVAL);
	}

	ifp = ifnet_byindex_ref(ifcfg->ifindex);
	if (NULL == ifp) {
		printf("could not find interface %s by index\n", name);
		return (EINVAL);
	}
	
	stat->ifi_ipackets   = ifp->if_data.ifi_ipackets;
	stat->ifi_ierrors    = ifp->if_data.ifi_ierrors;
	stat->ifi_opackets   = ifp->if_data.ifi_opackets;
	stat->ifi_oerrors    = ifp->if_data.ifi_oerrors;
	stat->ifi_collisions = ifp->if_data.ifi_collisions;
	stat->ifi_ibytes     = ifp->if_data.ifi_ibytes;
	stat->ifi_obytes     = ifp->if_data.ifi_obytes;
	stat->ifi_imcasts    = ifp->if_data.ifi_imcasts;
	stat->ifi_omcasts    = ifp->if_data.ifi_omcasts;
	stat->ifi_iqdrops    = ifp->if_data.ifi_iqdrops;
	stat->ifi_noproto    = ifp->if_data.ifi_noproto;
	stat->ifi_hwassist   = ifp->if_data.ifi_hwassist;
	stat->ifi_epoch      = ifp->if_data.ifi_epoch;
	stat->ifi_icopies    = ifp->if_data.ifi_icopies;
	stat->ifi_izcopies   = ifp->if_data.ifi_izcopies;
	stat->ifi_ocopies    = ifp->if_data.ifi_ocopies;
	stat->ifi_ozcopies   = ifp->if_data.ifi_ozcopies;

	if_rele(ifp);

	return (0);
}


void
uinet_gettcpstat(struct uinet_tcpstat *stat)
{
	*((struct tcpstat *)stat) = tcpstat;
}


char *
uinet_inet_ntoa(struct uinet_in_addr in, char *buf, unsigned int size)
{
	(void)size;

	return inet_ntoa_r(*((struct in_addr *)&in), buf); 
}


const char *
uinet_inet_ntop(int af, const void *src, char *dst, unsigned int size)
{
	return (inet_ntop(af, src, dst, size));
}


int
uinet_inet_pton(int af, const char *src, void *dst)
{
	return (inet_pton(af, src, dst));
}


static int
uinet_ifconfig_begin(struct socket **so, struct ifreq *ifr, const char *name)
{
	struct thread *td = curthread;
	struct uinet_config_if *ifcfg;
	int error;

	ifcfg = uinet_iffind_byname(name);
	if (NULL == ifcfg) {
		printf("could not find interface %s\n", name);
		return (EINVAL);
	}

	error = socreate(PF_INET, so, SOCK_DGRAM, 0, td->td_ucred, td);
	if (0 != error) {
		printf("ifconfig socket creation failed (%d)\n", error);
		return (error);
	}

	snprintf(ifr->ifr_name, sizeof(ifr->ifr_name), "%s", ifcfg->name);
	
	return (0);
}


static int
uinet_ifconfig_do(struct socket *so, unsigned long what, void *req)
{
	int error;

	error = ifioctl(so, what, (caddr_t)req, curthread);
	if (error != 0)
		printf("ifioctl 0x%08lx failed %d\n", what, error);

	return (error);
}


static void
uinet_ifconfig_end(struct socket *so)
{
	soclose(so);
}


int
uinet_interface_add_alias(const char *name, const char *addr, const char *braddr, const char *mask)
{
	struct socket *cfg_so;
	struct in_aliasreq ina;
	struct sockaddr_in template = {
		.sin_len = sizeof(struct sockaddr_in),
		.sin_family = AF_INET
	};
	int error;

	/*
	 * The cast of ina to (struct ifreq *) is safe because they both
	 * begin with the same size name field, and uinet_ifconfig_begin
	 * only touches the name field.
	 */
	error = uinet_ifconfig_begin(&cfg_so, (struct ifreq *)&ina, name);
	if (0 != error) {
		return (error);
	}

	ina.ifra_addr = template;
	if (inet_pton(AF_INET, addr, &ina.ifra_addr.sin_addr) <= 0) {
		error = EAFNOSUPPORT;
		goto out;
	}

	if (braddr == NULL || braddr[0] == '\0') {
		/* stack will set based on net class */
		ina.ifra_broadaddr.sin_len = 0;
	} else {
		ina.ifra_broadaddr = template;
		if (inet_pton(AF_INET, braddr, &ina.ifra_broadaddr.sin_addr) <= 0) {
			error = EAFNOSUPPORT;
			goto out;
		}
	}

	if (mask == NULL || mask[0] == '\0') {
		/* stack will set based on net class */
		ina.ifra_mask.sin_len = 0;
	} else {
		ina.ifra_mask = template;
		if (inet_pton(AF_INET, mask, &ina.ifra_mask.sin_addr) <= 0) {
			error = EAFNOSUPPORT;
			goto out;
		}
	}

	error = uinet_ifconfig_do(cfg_so, SIOCAIFADDR, &ina);

out:
	uinet_ifconfig_end(cfg_so);

	return (error);
}


int
uinet_interface_create(const char *name)
{
	struct socket *cfg_so;
	struct ifreq ifr;
	int error;

	error = uinet_ifconfig_begin(&cfg_so, &ifr, name);
	if (0 != error)
		return (error);

	error = uinet_ifconfig_do(cfg_so, SIOCIFCREATE, &ifr);

	uinet_ifconfig_end(cfg_so);

	return (error);
}


int
uinet_interface_up(const char *name, unsigned int promisc, unsigned int promiscinet)
{
	struct socket *cfg_so;
	struct ifreq ifr;
	int error;

	error = uinet_ifconfig_begin(&cfg_so, &ifr, name);
	if (0 != error)
		return (error);
	
	/* set interface to UP */

	error = uinet_ifconfig_do(cfg_so, SIOCGIFFLAGS, &ifr);
	if (0 == error) {
		ifr.ifr_flags |= IFF_UP;
		if (promisc)
			ifr.ifr_flagshigh |= IFF_PPROMISC >> 16;
		
		if (promiscinet)
			ifr.ifr_flagshigh |= IFF_PROMISCINET >> 16;
		
		error = uinet_ifconfig_do(cfg_so, SIOCSIFFLAGS, &ifr);
	}

	uinet_ifconfig_end(cfg_so);

	return (error);
}


int
uinet_mac_aton(const char *macstr, uint8_t *macout)
{

	unsigned int i;
	const char *p;
	char *endp;

	if ((NULL == macstr) || (macstr[0] == '\0')) {
		memset(macout, 0, ETHER_ADDR_LEN);
		return (0);
	}

	p = macstr;
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		macout[i] = strtoul(p, &endp, 16);
		if ((endp != &p[2]) ||					/* two hex digits */
		    ((i < ETHER_ADDR_LEN - 1) && (*endp != ':')) ||	/* followed by ':', unless last pair */
		    ((i == ETHER_ADDR_LEN - 1) && (*endp != '\0'))) {	/* followed by '\0', if last pair */
			return (1);
		}
		p = endp + 1;
	}

	return (0);
}


int
uinet_make_socket_passive(struct uinet_socket *so)
{
	struct socket *so_internal = (struct socket *)so;
	unsigned int optval, optlen;
	int error;

	optlen = sizeof(optval);

	optval = 1;
	if ((error = so_setsockopt(so_internal, SOL_SOCKET, SO_PASSIVE, &optval, optlen)))
		goto out;
	
	optval = 1;
	if ((error = so_setsockopt(so_internal, SOL_SOCKET, SO_REUSEPORT, &optval, optlen)))
		goto out;

	optval = 256*1024;
	if ((error = so_setsockopt(so_internal, SOL_SOCKET, SO_RCVBUF, &optval, optlen)))
		goto out;
out:
	return (error);
}


int
uinet_make_socket_promiscuous(struct uinet_socket *so, unsigned int fib)
{
	struct socket *so_internal = (struct socket *)so;
	unsigned int optval, optlen;
	int error;

	optlen = sizeof(optval);

	optval = 1;
	if ((error = so_setsockopt(so_internal, SOL_SOCKET, SO_PROMISC, &optval, optlen)))
		goto out;
	
	optval = fib;
	if ((error = so_setsockopt(so_internal, SOL_SOCKET, SO_SETFIB, &optval, optlen)))
		goto out;

	optval = 1;
	if ((error = so_setsockopt(so_internal, SOL_SOCKET, SO_REUSEPORT, &optval, optlen)))
		goto out;
	
	optval = 1;
	if ((error = so_setsockopt(so_internal, IPPROTO_IP, IP_BINDANY, &optval, optlen)))
		goto out;

out:
	return (error);
}


int
uinet_getl2info(struct uinet_socket *so, struct uinet_in_l2info *l2i)
{
	struct socket *so_internal = (struct socket *)so;
	struct in_l2info l2i_internal;
	size_t optlen;
	int error = 0;


	optlen = sizeof(*l2i);
	error = so_getsockopt(so_internal, SOL_SOCKET, SO_L2INFO, &l2i_internal, &optlen);
	if (0 == error) {
		memcpy(l2i, &l2i_internal, sizeof(*l2i));
	}

	return (error);
}


int
uinet_setl2info(struct uinet_socket *so, const struct uinet_in_l2info *l2i)
{
	struct socket *so_internal = (struct socket *)so;
	int error = 0;

	error = so_setsockopt(so_internal, SOL_SOCKET, SO_L2INFO, l2i, sizeof(*l2i));

	return (error);
}


int
uinet_setl2info2(struct uinet_socket *so, const uint8_t *local_addr, const uint8_t *foreign_addr,
		 uint16_t flags, const struct uinet_in_l2tagstack *tagstack)
{
	struct uinet_in_l2info l2i;

	memset(&l2i, 0, sizeof(l2i));

	if (local_addr)
		memcpy(l2i.inl2i_local_addr, local_addr, ETHER_ADDR_LEN);

	if (foreign_addr)
		memcpy(l2i.inl2i_foreign_addr, foreign_addr, ETHER_ADDR_LEN);

	l2i.inl2i_flags = flags;

	if (tagstack) {
		memcpy(&l2i.inl2i_tagstack, tagstack, sizeof(l2i.inl2i_tagstack));
	}

	return (uinet_setl2info(so, &l2i));
}


int
uinet_l2tagstack_cmp(const struct uinet_in_l2tagstack *ts1, const struct uinet_in_l2tagstack *ts2)
{
	return (in_promisc_tagcmp((const struct in_l2tagstack *)ts1, (const struct in_l2tagstack *)ts2));
}


uint32_t
uinet_l2tagstack_hash(const struct uinet_in_l2tagstack *ts)
{
	uint32_t hash;

	if (ts->inl2t_cnt) {
		hash = in_promisc_hash32(ts->inl2t_tags, 
					 ts->inl2t_masks,
					 ts->inl2t_cnt,
					 0);
	} else {
		hash = 0;
	}

	return (hash);
}


/*
 * This is really a version of kern_accept() without the file descriptor
 * bits.  As long as SS_NBIO is set on the listen socket, it does just what
 * you want to do in an upcall on that socket, so it's a better piece of
 * functionality to expose than just wrapping a bare soaccept().  If a blocking
 * syscall/poll style API comes later, this routine will serve that need as
 * well.
 */
int
uinet_soaccept(struct uinet_socket *listener, struct uinet_sockaddr **nam, struct uinet_socket **aso)
{
	struct socket *head = (struct socket *)listener;
	struct socket *so;
#ifdef PASSIVE_INET
	struct socket *peer_so;
#endif
	struct sockaddr *sa = NULL;
	int error = 0;

	if (nam)
		*nam = NULL;

	*aso = NULL;

	ACCEPT_LOCK();
	if ((head->so_state & SS_NBIO) && TAILQ_EMPTY(&head->so_comp)) {
		if (head->so_upcallprep.soup_accept != NULL) {
			head->so_upcallprep.soup_accept(head,
							head->so_upcallprep.soup_accept_arg);
		}
		ACCEPT_UNLOCK();
		error = EWOULDBLOCK;
		goto noconnection;
	}

	while (TAILQ_EMPTY(&head->so_comp) && head->so_error == 0) {
		if (head->so_rcv.sb_state & SBS_CANTRCVMORE) {
			head->so_error = ECONNABORTED;
			break;
		}
		error = msleep(&head->so_timeo, &accept_mtx, PSOCK | PCATCH,
		    "accept", 0);
		if (error) {
			ACCEPT_UNLOCK();
			goto noconnection;
		}
	}
	if (head->so_error) {
		error = head->so_error;
		head->so_error = 0;
		ACCEPT_UNLOCK();
		goto noconnection;
	}

	so = TAILQ_FIRST(&head->so_comp);
	KASSERT(!(so->so_qstate & SQ_INCOMP), ("uinet_soaccept: so_qstate SQ_INCOMP"));
	KASSERT(so->so_qstate & SQ_COMP, ("uinet_soaccept: so_qstate not SQ_COMP"));

	/*
	 * Before changing the flags on the socket, we have to bump the
	 * reference count.  Otherwise, if the protocol calls sofree(),
	 * the socket will be released due to a zero refcount.
	 */
	SOCK_LOCK(so);			/* soref() and so_state update */
	soref(so);			/* socket came from sonewconn() with an so_count of 0 */

	TAILQ_REMOVE(&head->so_comp, so, so_list);
	head->so_qlen--;
	so->so_state |= (head->so_state & SS_NBIO);
	so->so_qstate &= ~SQ_COMP;
	so->so_head = NULL;

	SOCK_UNLOCK(so);

#ifdef PASSIVE_INET
	peer_so = so->so_passive_peer;
	if (so->so_options & SO_PASSIVE) {
		KASSERT(peer_so, ("uinet_soaccept: passive socket has no peer"));
		SOCK_LOCK(peer_so);
		soref(peer_so);
		peer_so->so_state |=
		    (head->so_state & SS_NBIO) | SO_PASSIVECLNT;
		SOCK_UNLOCK(peer_so);
	}
#endif
	ACCEPT_UNLOCK();

	error = soaccept(so, &sa);
	if (error) {
#ifdef PASSIVE_INET
		if (peer_so)
			soclose(peer_so);
#endif
		soclose(so);
		return (error);
	}

	if (nam) {
		*nam = (struct uinet_sockaddr *)sa;
		sa = NULL;
	}

	*aso = (struct uinet_socket *)so;

noconnection:
	if (sa)
		free(sa, M_SONAME);

	return (error);
}


int
uinet_sobind(struct uinet_socket *so, struct uinet_sockaddr *nam)
{
	return sobind((struct socket *)so, (struct sockaddr *)nam, curthread);
}


int
uinet_soclose(struct uinet_socket *so)
{
	return soclose((struct socket *)so);
}


/*
 * This is really a version of kern_connect() without the file descriptor
 * bits.  As long as SS_NBIO is set on the socket, it does not block.  If a
 * blocking syscall/poll style API comes later, this routine will serve that
 * need as well.
 */
int
uinet_soconnect(struct uinet_socket *uso, struct uinet_sockaddr *nam)
{
	struct socket *so = (struct socket *)uso;
	int error;
	int interrupted = 0;

	if (so->so_state & SS_ISCONNECTING) {
		error = EALREADY;
		goto done1;
	}

	error = soconnect(so, (struct sockaddr *)nam, curthread);
	if (error)
		goto bad;
	if ((so->so_state & SS_NBIO) && (so->so_state & SS_ISCONNECTING)) {
		error = EINPROGRESS;
		goto done1;
	}
	SOCK_LOCK(so);
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		error = msleep(&so->so_timeo, SOCK_MTX(so), PSOCK | PCATCH,
		    "connec", 0);
		if (error) {
			if (error == EINTR || error == ERESTART)
				interrupted = 1;
			break;
		}
	}
	if (error == 0) {
		error = so->so_error;
		so->so_error = 0;
	}
	SOCK_UNLOCK(so);
bad:
	if (!interrupted)
		so->so_state &= ~SS_ISCONNECTING;
	if (error == ERESTART)
		error = EINTR;
done1:
	return (error);
}


int
uinet_socreate(int dom, struct uinet_socket **aso, int type, int proto)
{
	struct thread *td = curthread;

	return socreate(dom, (struct socket **)aso, type, proto, td->td_ucred, td);
}


void
uinet_sogetconninfo(struct uinet_socket *so, struct uinet_in_conninfo *inc)
{
	struct socket *so_internal = (struct socket *)so;
	struct inpcb *inp = sotoinpcb(so_internal);

	/* XXX do we really need the INFO lock here? */
	INP_INFO_RLOCK(inp->inp_pcbinfo);
	INP_RLOCK(inp);
	memcpy(inc, &sotoinpcb(so_internal)->inp_inc, sizeof(struct uinet_in_conninfo));
	INP_RUNLOCK(inp);
	INP_INFO_RUNLOCK(inp->inp_pcbinfo);
}


int
uinet_sogeterror(struct uinet_socket *so)
{
	struct socket *so_internal = (struct socket *)so;

	return (so_internal->so_error);
}


struct uinet_socket *
uinet_sogetpassivepeer(struct uinet_socket *so)
{
	struct socket *so_internal = (struct socket *)so;

	return ((struct uinet_socket *)(so_internal->so_passive_peer));
}


int
uinet_sogetsockopt(struct uinet_socket *so, int level, int optname, void *optval,
		   unsigned int *optlen)
{
	size_t local_optlen;
	int result;

	local_optlen = *optlen;
	result = so_getsockopt((struct socket *)so, level, optname, optval, &local_optlen);
	*optlen = local_optlen;

	return (result);
}


int
uinet_sogetstate(struct uinet_socket *so)
{
	struct socket *so_internal = (struct socket *)so;

	return (so_internal->so_state);
}


int
uinet_solisten(struct uinet_socket *so, int backlog)
{
	return solisten((struct socket *)so, backlog, curthread);
}


int
uinet_soreadable(struct uinet_socket *so, unsigned int in_upcall)
{
	struct socket *so_internal = (struct socket *)so;
	unsigned int avail; 
	int canread;

	if (so_internal->so_options & SO_ACCEPTCONN) {
		if (so_internal->so_error)
			canread = -1;
		else {
			ACCEPT_LOCK();
			canread = so_internal->so_qlen;
			ACCEPT_UNLOCK();
		}
	} else {
		if (!in_upcall)
			SOCKBUF_LOCK(&so_internal->so_rcv);

		avail = so_internal->so_rcv.sb_cc;
		if (avail || (!so_internal->so_error && !(so_internal->so_rcv.sb_state & SBS_CANTRCVMORE))) {
			if (avail > INT_MAX)
				canread = INT_MAX;
			else
				canread = avail;
		} else
			canread = -1;

		if (!in_upcall)
			SOCKBUF_UNLOCK(&so_internal->so_rcv);
	}

	return canread;
}


int
uinet_sowritable(struct uinet_socket *so, unsigned int in_upcall)
{
	struct socket *so_internal = (struct socket *)so;
	unsigned int space;
	int canwrite;

	if (so_internal->so_options & SO_ACCEPTCONN) {
		canwrite = 0;
	} else {
		if (!in_upcall)
			SOCKBUF_LOCK(&so_internal->so_snd);

		if ((so_internal->so_snd.sb_state & SBS_CANTSENDMORE) ||
		    so_internal->so_error ||
		    (so_internal->so_state & SS_ISDISCONNECTED)) {
			canwrite = -1;
		} else if ((so_internal->so_state & SS_ISCONNECTED) == 0) {
			canwrite = 0;
		} else {
			space = sbspace(&so_internal->so_snd);
			if (space > INT_MAX)
				canwrite = INT_MAX;
			else
				canwrite = space;
		}

		if (!in_upcall)
			SOCKBUF_UNLOCK(&so_internal->so_snd);
	}

	return canwrite;
}


int
uinet_soallocuserctx(struct uinet_socket *so)
{
	struct socket *so_internal = (struct socket *)so;

	return souserctx_alloc(so_internal);
}


void *
uinet_sogetuserctx(struct uinet_socket *so, int key)
{
	struct socket *so_internal = (struct socket *)so;

	if ((key >= 0) && (key < SOMAXUSERCTX))
		return (so_internal->so_user_ctx[key]);
	else
		return (NULL);
		
}


void
uinet_sosetuserctx(struct uinet_socket *so, int key, void *ctx)
{
	struct socket *so_internal = (struct socket *)so;

	if ((key >= 0) && (key < SOMAXUSERCTX))
		so_internal->so_user_ctx[key] = ctx;
}


int
uinet_soreceive(struct uinet_socket *so, struct uinet_sockaddr **psa, struct uinet_uio *uio, int *flagsp)
{
	struct iovec iov[uio->uio_iovcnt];
	struct uio uio_internal;
	int i;
	int result;

	for (i = 0; i < uio->uio_iovcnt; i++) {
		iov[i].iov_base = uio->uio_iov[i].iov_base;
		iov[i].iov_len = uio->uio_iov[i].iov_len;
	}
	uio_internal.uio_iov = iov;
	uio_internal.uio_iovcnt = uio->uio_iovcnt;
	uio_internal.uio_offset = uio->uio_offset;
	uio_internal.uio_resid = uio->uio_resid;
	uio_internal.uio_segflg = UIO_SYSSPACE;
	uio_internal.uio_rw = UIO_READ;
	uio_internal.uio_td = curthread;
	
	result = soreceive((struct socket *)so, (struct sockaddr **)psa, &uio_internal, NULL, NULL, flagsp);

	uio->uio_resid = uio_internal.uio_resid;

	return (result);
}


void
uinet_sosetnonblocking(struct uinet_socket *so, unsigned int nonblocking)
{
	struct socket *so_internal = (struct socket *)so;

	if (nonblocking) {
		so_internal->so_state |= SS_NBIO;
	} else {
		so_internal->so_state &= ~SS_NBIO;
	}

}


int
uinet_sosetsockopt(struct uinet_socket *so, int level, int optname, void *optval,
		   unsigned int optlen)
{
	return so_setsockopt((struct socket *)so, level, optname, optval, optlen);
}


void
uinet_sosetupcallprep(struct uinet_socket *so,
		      void (*soup_accept)(struct uinet_socket *, void *), void *soup_accept_arg,
		      void (*soup_receive)(struct uinet_socket *, void *, int64_t, int64_t), void *soup_receive_arg,
		      void (*soup_send)(struct uinet_socket *, void *, int64_t), void *soup_send_arg)
{
	struct socket *so_internal = (struct socket *)so;

	so_internal->so_upcallprep.soup_accept = (void (*)(struct socket *, void *))soup_accept;
	so_internal->so_upcallprep.soup_accept_arg = soup_accept_arg;
	so_internal->so_upcallprep.soup_receive = (void (*)(struct socket *, void *, int64_t, int64_t))soup_receive;
	so_internal->so_upcallprep.soup_receive_arg = soup_receive_arg;
	so_internal->so_upcallprep.soup_send = (void (*)(struct socket *, void *, int64_t))soup_send;
	so_internal->so_upcallprep.soup_send_arg = soup_send_arg;
}



int
uinet_sosend(struct uinet_socket *so, struct uinet_sockaddr *addr, struct uinet_uio *uio, int flags)
{
	struct iovec iov[uio->uio_iovcnt];
	struct uio uio_internal;
	int i;
	int result;

	for (i = 0; i < uio->uio_iovcnt; i++) {
		iov[i].iov_base = uio->uio_iov[i].iov_base;
		iov[i].iov_len = uio->uio_iov[i].iov_len;
	}
	uio_internal.uio_iov = iov;
	uio_internal.uio_iovcnt = uio->uio_iovcnt;
	uio_internal.uio_offset = uio->uio_offset;
	uio_internal.uio_resid = uio->uio_resid;
	uio_internal.uio_segflg = UIO_SYSSPACE;
	uio_internal.uio_rw = UIO_WRITE;
	uio_internal.uio_td = curthread;

	result = sosend((struct socket *)so, (struct sockaddr *)addr, &uio_internal, NULL, NULL, flags, curthread);

	uio->uio_resid = uio_internal.uio_resid;

	return (result);
}


int
uinet_soshutdown(struct uinet_socket *so, int how)
{
	return soshutdown((struct socket *)so, how);
}


int
uinet_sogetpeeraddr(struct uinet_socket *so, struct uinet_sockaddr **sa)
{
	struct socket *so_internal = (struct socket *)so;

	*sa = NULL;
	return (*so_internal->so_proto->pr_usrreqs->pru_peeraddr)(so_internal, (struct sockaddr **)sa);
}


int
uinet_sogetsockaddr(struct uinet_socket *so, struct uinet_sockaddr **sa)
{
	struct socket *so_internal = (struct socket *)so;

	*sa = NULL;
	return (*so_internal->so_proto->pr_usrreqs->pru_sockaddr)(so_internal, (struct sockaddr **)sa);
}


void
uinet_free_sockaddr(struct uinet_sockaddr *sa)
{
	free(sa, M_SONAME);
}


void
uinet_soupcall_lock(struct uinet_socket *so, int which)
{
	struct socket *so_internal = (struct socket *)so;
	struct sockbuf *sb;

	switch(which) {
	case UINET_SO_RCV:
		sb = &so_internal->so_rcv;
		break;
	case UINET_SO_SND:
		sb = &so_internal->so_snd;
		break;
	default:
		return;
	}
	
	SOCKBUF_LOCK(sb);
}


void
uinet_soupcall_unlock(struct uinet_socket *so, int which)
{
	struct socket *so_internal = (struct socket *)so;
	struct sockbuf *sb;

	switch(which) {
	case UINET_SO_RCV:
		sb = &so_internal->so_rcv;
		break;
	case UINET_SO_SND:
		sb = &so_internal->so_snd;
		break;
	default:
		return;
	}
	
	SOCKBUF_UNLOCK(sb);
}


void
uinet_soupcall_set(struct uinet_socket *so, int which,
		   int (*func)(struct uinet_socket *, void *, int), void *arg)
{
	struct socket *so_internal = (struct socket *)so;
	struct sockbuf *sb;

	switch(which) {
	case UINET_SO_RCV:
		sb = &so_internal->so_rcv;
		break;
	case UINET_SO_SND:
		sb = &so_internal->so_snd;
		break;
	default:
		return;
	}

	SOCKBUF_LOCK(sb);
	uinet_soupcall_set_locked(so, which, func, arg);
	SOCKBUF_UNLOCK(sb);
}


void
uinet_soupcall_set_locked(struct uinet_socket *so, int which,
			  int (*func)(struct uinet_socket *, void *, int), void *arg)
{
	struct socket *so_internal = (struct socket *)so;
	soupcall_set(so_internal, which, (int (*)(struct socket *, void *, int))func, arg);
}


void
uinet_soupcall_clear(struct uinet_socket *so, int which)
{
	struct socket *so_internal = (struct socket *)so;
	struct sockbuf *sb;

	switch(which) {
	case UINET_SO_RCV:
		sb = &so_internal->so_rcv;
		break;
	case UINET_SO_SND:
		sb = &so_internal->so_snd;
		break;
	default:
		return;
	}

	SOCKBUF_LOCK(sb);
	uinet_soupcall_clear_locked(so, which);
	SOCKBUF_UNLOCK(sb);

}


void
uinet_soupcall_clear_locked(struct uinet_socket *so, int which)
{
	struct socket *so_internal = (struct socket *)so;
	soupcall_clear(so_internal, which);
}


static int
uinet_api_synfilter_callback(struct inpcb *inp, void *inst_arg, struct syn_filter_cbarg *arg)
{
	struct uinet_api_synfilter_ctx *ctx = inst_arg;
	
	return (ctx->callback((struct uinet_socket *)inp->inp_socket, ctx->arg, arg));
}

static void *
uinet_api_synfilter_ctor(struct inpcb *inp, char *arg)
{
	void *result;
	memcpy(&result, arg, sizeof(result));
	return result;
}


static void
uinet_api_synfilter_dtor(struct inpcb *inp, void *arg)
{
	free(arg, M_DEVBUF);
}


static struct syn_filter synf_uinet_api = {
	"uinet_api",
	uinet_api_synfilter_callback,
	uinet_api_synfilter_ctor,
	uinet_api_synfilter_dtor,
};

static moduledata_t synf_uinet_api_mod = {
	"uinet_api_synf",
	syn_filter_generic_mod_event,
	&synf_uinet_api
};

DECLARE_MODULE(synf_uinet_api, synf_uinet_api_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);


uinet_synf_deferral_t
uinet_synfilter_deferral_alloc(struct uinet_socket *so, uinet_api_synfilter_cookie_t cookie)
{
	struct syn_filter_cbarg *cbarg = cookie;
	struct syn_filter_cbarg *result;
	
	/* XXX might want to get these from a pool for better speed */
	result = malloc(sizeof(*result), M_DEVBUF, M_WAITOK);
	*result = *cbarg;

	return result;
}


void
uinet_synfilter_deferral_free(uinet_synf_deferral_t deferral)
{
	free(deferral, M_DEVBUF);
}


uinet_api_synfilter_cookie_t
uinet_synfilter_deferral_get_cookie(uinet_synf_deferral_t deferral)
{
	return ((uinet_api_synfilter_cookie_t)deferral);
}


int
uinet_synfilter_deferral_deliver(struct uinet_socket *so, uinet_synf_deferral_t deferral, int decision)
{
	struct socket *so_internal = (struct socket *)so;
	struct syn_filter_cbarg *cbarg = deferral;
	int error;

	cbarg->decision = decision;
	error = so_setsockopt(so_internal, IPPROTO_IP, IP_SYNFILTER_RESULT, cbarg, sizeof(*cbarg));

	free(deferral, M_DEVBUF);
	
	return (error);
}


void
uinet_synfilter_getconninfo(uinet_api_synfilter_cookie_t cookie, struct uinet_in_conninfo *inc)
{
	struct syn_filter_cbarg *cbarg = cookie;
	memcpy(inc, &cbarg->inc, sizeof(struct uinet_in_conninfo));
}


void
uinet_synfilter_getl2info(uinet_api_synfilter_cookie_t cookie, struct uinet_in_l2info *l2i)
{
	struct syn_filter_cbarg *cbarg = cookie;

	memcpy(l2i, cbarg->l2i, sizeof(*l2i));
}


void
uinet_synfilter_setl2info(uinet_api_synfilter_cookie_t cookie, struct uinet_in_l2info *l2i)
{
	struct syn_filter_cbarg *cbarg = cookie;

	memcpy(cbarg->l2i, l2i, sizeof(*l2i));
}


void
uinet_synfilter_setaltfib(uinet_api_synfilter_cookie_t cookie, unsigned int altfib)
{
	struct syn_filter_cbarg *cbarg = cookie;
	
	cbarg->altfib = altfib;
}


void
uinet_synfilter_go_active_on_timeout(uinet_api_synfilter_cookie_t cookie, unsigned int ms)
{
	struct syn_filter_cbarg *cbarg = cookie;
	
	cbarg->inc.inc_flags |= INC_CONVONTMO;
	cbarg->initial_timeout = (ms > INT_MAX / hz) ? INT_MAX / 1000 : (ms * hz) / 1000;
}


int
uinet_synfilter_install(struct uinet_socket *so, uinet_api_synfilter_callback_t callback, void *arg)
{
	struct socket *so_internal = (struct socket *)so;
	struct uinet_api_synfilter_ctx *ctx;
	struct syn_filter_optarg synf;
	int error = 0;

	ctx = malloc(sizeof(*ctx), M_DEVBUF, M_WAITOK);
	ctx->callback = callback;
	ctx->arg = arg;

	memset(&synf, 0, sizeof(synf));
	strlcpy(synf.sfa_name, synf_uinet_api.synf_name, SYNF_NAME_MAX);
	memcpy(synf.sfa_arg, &ctx, sizeof(ctx));

	if ((error = so_setsockopt(so_internal, IPPROTO_IP, IP_SYNFILTER, &synf, sizeof(synf)))) {
		free(ctx, M_DEVBUF);
	}

	return (error);
}


int
uinet_sysctlbyname(char *name, char *oldp, size_t *oldplen,
    char *newp, size_t newplen, size_t *retval, int flags)
{
	int error;

	error = kernel_sysctlbyname(curthread, name, oldp, oldplen,
	    newp, newplen, retval, flags);
	return (error);
}


int
uinet_sysctl(int *name, u_int namelen, void *oldp, size_t *oldplen,
    void *newp, size_t newplen, size_t *retval, int flags)
{
	int error;

	error = kernel_sysctl(curthread, name, namelen, oldp, oldplen,
	    newp, newplen, retval, flags);
	return (error);
}

static uinet_pfil_cb_t g_uinet_pfil_cb = NULL;
static void * g_uinet_pfil_cbdata = NULL;
static struct ifnet *g_uinet_pfil_ifp = NULL;

/*
 * Hook for processing IPv4 frames.
 */
static int
uinet_pfil_in_hook_v4(void *arg, struct mbuf **m, struct ifnet *ifp, int dir,
    struct inpcb *inp)
{
	struct ifl2info *l2i_tag;
	struct uinet_in_l2info uinet_l2i;

	/*
	 * No hook? Turf out.
	 */
	if (g_uinet_pfil_cb == NULL)
		return (0);

	/*
	 * Check if the ifp matches the ifp name we're interested in.
	 * When doing bridging we will see incoming frames for the
	 * physical incoming interface (eg netmap0, netmap1) and
	 * the bridge interface (bridge0).  We may actually not want
	 * that.
	 */
	if (g_uinet_pfil_ifp && (g_uinet_pfil_ifp != ifp))
		return (0);

	/*
	 * See if there's L2 information for this frame.
	 */
	l2i_tag = (struct ifl2info *)m_tag_locate(*m,
	    MTAG_PROMISCINET,
	    MTAG_PROMISCINET_L2INFO,
	    NULL);

#if 0
	if (l2i_tag == NULL) {
		printf("%s: no L2 information\n",
		    __func__);
	} else {
		printf("%s: src=%s",
		    __func__,
		    ether_sprintf(l2i_tag->ifl2i_info.inl2i_local_addr));
		printf(" dst=%s\n",
		    ether_sprintf(l2i_tag->ifl2i_info.inl2i_foreign_addr));
	}
#endif

	/*
	 * Populate the libuinet L2 header type
	 *
	 * XXX this should be a method!
	 */
	if (l2i_tag != NULL)
		memcpy(&uinet_l2i, &l2i_tag->ifl2i_info, sizeof(uinet_l2i));

	/*
	 * Call our callback to process the frame
	 */
	g_uinet_pfil_cb((const struct uinet_mbuf *) *m,
	    l2i_tag != NULL ? &uinet_l2i : NULL);

	/* Pass all for now */
	return (0);
}

/*
 * Register a single hook for the AF_INET pfil.
 */
int
uinet_register_pfil_in(uinet_pfil_cb_t cb, void *arg, const char *ifname)
{
	int error;
	VNET_ITERATOR_DECL(vnet_iter);
	struct pfil_head *pfh;

	if (g_uinet_pfil_cb != NULL) {
		printf("%s: callback already registered!\n", __func__);
		return (-1);
	}

	g_uinet_pfil_cb = cb;
	g_uinet_pfil_cbdata = arg;

	/* Take a reference to the ifnet if we're interested in it */
	if (ifname != NULL) {
		g_uinet_pfil_ifp = ifunit_ref(ifname);
	}

	VNET_LIST_RLOCK();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		/* XXX TODO: ipv6 */
		pfh = pfil_head_get(PFIL_TYPE_AF, AF_INET);
		error = pfil_add_hook(uinet_pfil_in_hook_v4, NULL,
		    PFIL_IN | PFIL_WAITOK, pfh);
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK();
	return (0);
}

/*
 * Get a pointer to the given mbuf data.
 *
 * This only grabs the pointer to this first mbuf; not the whole
 * chain worth of data.  That's a different API (which likely should
 * be implemented at some point.)
 */
const char *
uinet_mbuf_data(const struct uinet_mbuf *m)
{
	const struct mbuf *mb = (const struct mbuf *) m;

	return mtod(mb, const char *);
}

size_t
uinet_mbuf_len(const struct uinet_mbuf *m)
{
	const struct mbuf *mb = (const struct mbuf *) m;

	return (mb->m_len);
}

/*
 * Queue this buffer for transmit.
 *
 * The transmit path will take a copy of the data; it won't reference it.
 *
 * Returns 0 on OK, non-zero on error.
 *
 * Note: this reaches into kernel code, so you need to have set up all
 * the possible transmit threads as uinet threads, or this call will
 * fail.
 */
int
uinet_if_xmit(void *cookie, const char *buf, int len)
{
	struct uinet_config_if *cif = cookie;
	struct mbuf *m;
	struct ifnet *ifp;

	/* Create mbuf; populate it with the given buffer */
	m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);

	if (! m_append(m, (size_t) len, (void *) buf)) {
		m_freem(m);
		return (ENOMEM);
	}

	/* Call if_transmit() on the given interface */
	ifp = cif->ifp;
	return ((ifp->if_transmit)(ifp, m));
}

int
uinet_lock_log_set_file(const char *file)
{

	uhi_lock_log_set_file(file);
	return (0);
}

int
uinet_lock_log_enable(void)
{

	uhi_lock_log_enable();
	return (0);
}

int
uinet_lock_log_disable(void)
{

	uhi_lock_log_disable();
	return (0);
}
