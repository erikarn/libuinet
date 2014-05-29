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
	struct ifnet *ifp;
	const struct uinet_config_if *cfg;
	uint8_t addr[ETHER_ADDR_LEN];

	/* XXX TODO: more useful state? */
};

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
	 */

	 /*
	  * Allocate netif context.
	  */

	/*
	 * Setup netif methods.
	 */

	/*
	 * Add the given child interfaces to the bridge.
	 */

	/*
	 * Link uinet cfg state back to the newly setup ifnet.
	 */

	return (0);

fail:
	/* XXX TODO: deregister child interfaces */
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
