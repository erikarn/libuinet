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

struct if_bridge_softc {
	struct ifnet *sc_ifp;
	const struct uinet_config_if *cfg;
	uint8_t addr[ETHER_ADDR_LEN];

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

	/* XXX for now, consume */
	m_freem(m);
	return (NULL);
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

	/*
	 * Add the given child interfaces to the bridge.
	 */

	/*
	 * Link uinet cfg state back to the newly setup ifnet.
	 */

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
