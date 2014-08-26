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

#if defined(__linux__)
/*
 * To expose required facilities in net/if.h.
 */
#define _GNU_SOURCE
#endif /* __linux__ */

#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pcap/pcap.h>

#include "uinet_host_interface.h"
#include "uinet_if_pcap_host.h"


struct if_pcap_host_context {
	pcap_t *p;
	pcap_dumper_t *pd;	/* for writing out to a file */
	pcap_t *wp;		/* write dump context */
	int isfile;		/* for reading */
	int write_file;
	const char *ifname;
	if_pcap_handler pkthandler;
	void *pkthandlerarg;
	uint64_t last_packet_delivery;
	uint64_t last_packet_timestamp;		
	char errbuf[PCAP_ERRBUF_SIZE];
};

/*
 * Open an outgoing file for outbound data.
 *
 * This may be used during debugging to capture
 * all data being sent out rather than writing
 * it to the network.
 *
 * Returns 0 if OK, < 0 if error.
 */
int
if_pcap_write_to_file(struct if_pcap_host_context *ctx, const char *fname)
{
	pcap_t *dp;
	pcap_dumper_t *pd;

	if (ctx->write_file != 0) {
		printf("%s: already opened for writing!\n", __func__);
		return (-1);
	}

	/* Create a fake DLT for our subsequent file open */
	/* ethernet DLT, 65536 byte snaplen */
	dp = pcap_open_dead(DLT_EN10MB, 65536);
	if (dp == NULL) {
		printf("%s: couldn't create pcap DLT\n", __func__);
		return (-1);
	}

	/* Open file */
	pd = pcap_dump_open(dp, fname);
	if (pd == NULL) {
		printf("%s: couldn't create dumpfile\n", __func__);
		pcap_close(dp);
		return (-1);
	}

	/* We're ready! */
	ctx->pd = pd;
	ctx->wp = dp;
	ctx->write_file = 1;

	return (0);
}

/*
 * Close an open file write context.
 *
 * Returns 0 on OK, < 0 on error.
 */
int
if_pcap_close_write_to_file(struct if_pcap_host_context *ctx)
{

	/* Not a fatal error */
	if (ctx->write_file == 0)
		return (0);

	pcap_dump_flush(ctx->pd);
	pcap_dump_close(ctx->pd);
	pcap_close(ctx->wp);
	ctx->pd = NULL;
	ctx->wp = NULL;
	ctx->write_file = 0;

	return (0);
}


struct if_pcap_host_context *
if_pcap_create_handle(const char *ifname, unsigned int isfile, if_pcap_handler handler, void *handlerarg)
{
	struct if_pcap_host_context *ctx;
	int dlt;

	ctx = calloc(1, sizeof(*ctx));
	if (NULL == ctx)
		goto fail;

	ctx->isfile = isfile;
	ctx->pkthandler = handler;
	ctx->pkthandlerarg = handlerarg;

	if (ctx->isfile) {
		ctx->p = pcap_open_offline(ifname, ctx->errbuf);
		if (NULL == ctx->p)
			goto fail;
	} else {
		ctx->p = pcap_create(ifname, ctx->errbuf);
		if (NULL == ctx->p)
			goto fail;

		if (-1 == pcap_setdirection(ctx->p, PCAP_D_IN)) {
			printf("Could not restrict pcap capture to input on %s\n", ifname);
			goto fail;
		}

		pcap_set_timeout(ctx->p, 1);
		pcap_set_snaplen(ctx->p, 65535);
		pcap_set_promisc(ctx->p, 1);

		switch (pcap_activate(ctx->p)) {
		case 0:
			break;
		case PCAP_WARNING_PROMISC_NOTSUP:
			printf("Promiscuous mode not supported on %s: %s\n", ifname, pcap_geterr(ctx->p));
			break;
		case PCAP_WARNING:
			printf("Warning while activating pcap capture on %s: %s\n", ifname, pcap_geterr(ctx->p));
			break;
		case PCAP_ERROR_NO_SUCH_DEVICE:
		case PCAP_ERROR_PERM_DENIED:
			printf("Error activating pcap capture on %s: %s\n", ifname, pcap_geterr(ctx->p));
			/* FALLTHOUGH */
		default:
			goto fail;
			break;
		}

		dlt = pcap_datalink(ctx->p);
		if (DLT_EN10MB != dlt) {
			printf("Data link type on %s is %d, only %d supported\n", ifname, dlt, DLT_EN10MB);
			goto fail;
		}
	}

	return (ctx);

fail:
	if (ctx)
		free(ctx);

	return (NULL);
}


void
if_pcap_destroy_handle(struct if_pcap_host_context *ctx)
{

	pcap_close(ctx->p);
	(void) if_pcap_close_write_to_file(ctx);
	free(ctx);
}


int
if_pcap_sendpacket(struct if_pcap_host_context *ctx, const uint8_t *buf, unsigned int size)
{
	struct pcap_pkthdr ph;
	int64_t sec;
	long nsec;

	if (ctx->isfile == 0) {
		return pcap_sendpacket(ctx->p, buf, size);
	}

	/* Don't write if we haven't explicitly enabled an output file */
	if (ctx->write_file == 0) {
		printf("if_pcap_send: Packet send attempt in file mode with no output file\n");
		return (-1);
	}

	/* ok, setup ph with current timestamp info and move on */
	uhi_clock_gettime(UHI_CLOCK_MONOTONIC, &sec, &nsec);
	ph.ts.tv_sec = sec;
	ph.ts.tv_usec = (long) nsec / (long) 1000L;
	ph.caplen = size;
	ph.len = size;

	pcap_dump((u_char *) ctx->pd, &ph, buf);

	//pcap_dump_flush(ctx->pd);

	return (0);
}

static void
if_pcap_packet_handler(struct if_pcap_host_context *ctx, const struct pcap_pkthdr *pkthdr, const unsigned char *pkt)
{
	uint64_t timestamp;
	uint64_t now;
	uint64_t time_since_last_delivery;
	uint64_t time_since_last_capture;		

	if (ctx->isfile) {
		timestamp = (uint64_t)pkthdr->ts.tv_sec * 1000000000ULL + (uint64_t)pkthdr->ts.tv_usec * 1000;
		now = uhi_clock_gettime_ns(UHI_CLOCK_MONOTONIC);

		time_since_last_delivery = now - ctx->last_packet_delivery;
		time_since_last_capture = timestamp - ctx->last_packet_timestamp;

		if ((0 != ctx->last_packet_delivery) && 
		    (time_since_last_delivery < time_since_last_capture))
			uhi_nanosleep(time_since_last_capture - time_since_last_delivery);
		
		ctx->last_packet_delivery = now;
		ctx->last_packet_timestamp = timestamp;
	}

	ctx->pkthandler(ctx->pkthandlerarg, pkt, pkthdr->caplen);
}


int
if_pcap_loop(struct if_pcap_host_context *ctx)
{
	return pcap_loop(ctx->p, -1, (pcap_handler)if_pcap_packet_handler, (unsigned char *)ctx);
}


