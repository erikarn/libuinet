/*
 * Copyright (c) 2014 Adrian Chadd, Norse Inc.  All Rights Reserved.
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

#include <sys/ctype.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/sched.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <net/if_tap.h>
#include <net/if_dl.h>

#include <machine/atomic.h>

#include "uinet_config_internal.h"
#include "uinet_host_interface.h"
#include "uinet_if_bridge.h"

/*
 * This implements a two-port, transparent bridge interface.
 *
 * It's designed to just be a dumb conduit between two other
 * physical interfaces.  It's not supposed to be a fully fledged
 * bridge.
 */

extern	struct mbuf *(*bridge_input_p)(struct ifnet *, struct mbuf *);
extern	int (*bridge_output_p)(struct ifnet *, struct mbuf *,
		struct sockaddr *, struct rtentry *);

struct if_bridge_member;

struct if_bridge_member {
	LIST_ENTRY(if_bridge_member) bif_next;
	struct ifnet *ifp;
};

struct if_bridge_softc {
	struct ifnet *sc_ifp;
	const struct uinet_config_if *cfg;
	uint8_t addr[ETHER_ADDR_LEN];

	struct mtx sc_mtx;
	LIST_HEAD(, if_bridge_member) sc_iflist;   /* member interface list */

	/* XXX TODO: more useful state? */
};

static int bridge_if_count = 0;

/*
 * Process an incoming frame.  This gets called
 * from the child device ether_input path.
 */
static struct mbuf *
if_bridge_input(struct ifnet *ifp, struct mbuf *m)
{
	struct if_bridge_softc *sc;
	struct ifnet *bifp;
	struct mbuf *mc2;

	sc = ifp->if_bridge;
	bifp = sc->sc_ifp;

	printf("%s: m=%p: called\n", __func__, m);

	/*
	 * XXX todo: hook in a packet lookup function to
	 * let bits of code decide if the packet should be
	 * bridged.
	 */

	/* Duplicate; pass up to the stack */
	mc2 = m_dup(m, M_DONTWAIT);
	if (mc2 != NULL) {
		/* Keep the layer3 header aligned */
		int i = min(mc2->m_pkthdr.len, max_protohdr);
		mc2 = m_copyup(mc2, i, ETHER_ALIGN);
	}
	if (mc2 != NULL) {
		mc2->m_pkthdr.rcvif = bifp;
		(*bifp->if_input)(bifp, mc2);
	}

	/* Return the original packet for local processing. */
	return (m);
}

/*
 */
static int
if_bridge_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *sa,
    struct rtentry *rt)
{

	/* For now, we consume the frame */
	m_freem(m);
	return (0);
}

static void
if_bridge_init(void *arg)
{
	struct if_bridge_softc *sc = arg;
	struct ifnet *ifp = sc->sc_ifp;

	ifp->if_drv_flags = IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

static void
if_bridge_stop(struct if_bridge_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING|IFF_DRV_OACTIVE);
}

static int
if_bridge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	int error = 0;
	struct if_bridge_softc *sc = ifp->if_softc;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			if_bridge_init(sc);
		else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			if_bridge_stop(sc);
			break;
		default:
			error = ether_ioctl(ifp, cmd, data);
		break;
		}
	return (error);
}

static int
if_bridge_transmit(struct ifnet *ifp, struct mbuf *m)
{

	/* XXX for now, free */
	m_freem(m);
	return (ENOBUFS);
}

static void
if_bridge_qflush(struct ifnet *ifp)
{

}

static int
if_bridge_existsm_locked(struct if_bridge_softc *sc, struct ifnet *nifp)
{
	struct if_bridge_member *bif;

	/* XXX assert locked */
	LIST_FOREACH(bif, &sc->sc_iflist, bif_next) {
		if (bif->ifp == nifp)
			return (1);
	}
	return (0);
}

static int
if_bridge_addm(struct if_bridge_softc *sc, const char *ifname)
{
	struct ifnet *nifp = NULL;
	struct if_bridge_member *bif;
	int error = 0;

	/* Do lookup */
	nifp = ifunit_ref(ifname);
	if (nifp == NULL) {
		printf("%s: '%s' not found\n",
		    __func__,
		    ifname);
		return (ENOENT);
	}

	mtx_lock(&sc->sc_mtx);

	/* See if this exists. Don't double-add */
	if (if_bridge_existsm_locked(sc, nifp)) {
		printf("%s: '%s' already is in this bridge\n",
		    __func__,
		    ifname);
		error = EINVAL;
		goto fail;
	}

	/* Is it a member of ANY bridge? */
	if (nifp->if_bridge != NULL) {
		printf("%s: '%s' is already in _a_ bridge\n",
		    __func__,
		    ifname);
		error = EBUSY;
		goto fail;
	}

	/* Allocate bridge-member entry, add to list */
	bif = malloc(sizeof(struct if_bridge_member), M_DEVBUF, M_NOWAIT);
	if (bif == NULL) {
		printf("%s: failed to malloc", __func__);
		error = ENOMEM;
		goto fail;
	}

	/* Add to list; link back from the ifnet to the parent bridge */
	bif->ifp = nifp;
	LIST_INSERT_HEAD(&sc->sc_iflist, bif, bif_next);
	nifp->if_bridge = sc;

	mtx_unlock(&sc->sc_mtx);

	/* Make promisc */
	error = ifpromisc(nifp, 1);
	if (error != 0) {
		mtx_lock(&sc->sc_mtx);
		/* XXX methodize */
		LIST_REMOVE(bif, bif_next);
		mtx_unlock(&sc->sc_mtx);
		free(bif, M_DEVBUF);
		printf("%s: '%s' couldn't make it promisc!\n", __func__, ifname);
		error = EINVAL;
		goto fail;
	}

	printf("%s: added '%s' to bridge\n", __func__, ifname);

	/* Done! */
	return (0);
fail:
	mtx_unlock(&sc->sc_mtx);
	/* Free reference */
	if (nifp)
		if_rele(nifp);

	return (error);
}

int
if_bridge_attach(struct uinet_config_if *cfg)
{
	struct if_bridge_softc *sc = NULL;
	int error = 0;

	if (NULL == cfg->configstr) {
		error = EINVAL;
		goto fail;
	}

	printf("%s: configstr=%s\n", __func__, cfg->configstr);

	sc = malloc(sizeof(struct if_bridge_softc), M_DEVBUF, M_WAITOK);
	if (sc == NULL) {
		printf("%s: malloc failed\n", __func__);
		error = ENOMEM;
		goto fail;
	}

	/* Set the interface name */
	snprintf(cfg->name, sizeof(cfg->name), "bridge%u", bridge_if_count);
	bridge_if_count++;

	sc->cfg = cfg;

	/*
	 * The ethernet path has a bunch of hard-coded
	 * bridge function pointers for whatever implements
	 * bridging.  It's a hack, but in order to get
	 * frames and link status changes, we need to
	 * also do the same.
	 */
	bridge_input_p = if_bridge_input;
	bridge_output_p = if_bridge_output;

	/*
	 * Setup local MAC address from configuration.
	 * XXX TODO
	 */
	sc->addr[0] = 0x62;
	sc->addr[1] = 0x73;
	sc->addr[2] = 0x64;
	sc->addr[3] = arc4random();
	sc->addr[4] = arc4random();
	sc->addr[5] = arc4random();

	 /*
	  * Allocate netif context.
	  */
	sc->sc_ifp = if_alloc(IFT_ETHER);
	if (sc->sc_ifp == NULL) {
		printf("%s: if_alloc failed", __func__);
		error = ENOMEM;
		goto fail;
	}
	sc->sc_ifp->if_softc = sc;

	/*
	 * Setup basic flags and such.
	 */
	if_initname(sc->sc_ifp, sc->cfg->name, IF_DUNIT_NONE);
	sc->sc_ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	sc->sc_ifp->if_mtu = 1500;	/* XXX verify! */

	/*
	 * Setup netif methods.
	 */
	sc->sc_ifp->if_init = if_bridge_init;
	sc->sc_ifp->if_ioctl = if_bridge_ioctl;
	sc->sc_ifp->if_transmit = if_bridge_transmit;
	sc->sc_ifp->if_qflush = if_bridge_qflush;

	sc->sc_ifp->if_fib = sc->cfg->cdom;

	/* Set local MAC now */
	ether_ifattach(sc->sc_ifp, sc->addr);
	sc->sc_ifp->if_capabilities = sc->sc_ifp->if_capenable = 0;

	/* Mutex protecting the bridge list */
	mtx_init(&sc->sc_mtx, "if_bridge", NULL, MTX_DEF);

	/* This is our list of child interfaces */
	LIST_INIT(&sc->sc_iflist);

	/*
	 * Add the given child interfaces to the bridge.
	 * (whilst also putting them into promisc mode.)
	 */
	(void) if_bridge_addm(sc, "netmap0");
	(void) if_bridge_addm(sc, "netmap1");

	/*
	 * Link uinet cfg state back to the newly setup ifnet.
	 */
	cfg->ifindex = sc->sc_ifp->if_index;
	cfg->ifdata = sc;

	return (0);

fail:
	/* XXX TODO: deregister child interfaces */
	if (sc && sc->sc_ifp)
		if_free(sc->sc_ifp);
	if (sc)
		free(sc, M_DEVBUF);
	return (error);

}

int
if_bridge_detach(struct uinet_config_if *cfg)
{
	/* XXX TODO */
	return (0);
}
