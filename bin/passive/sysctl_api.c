/*
 * Copyright (c) 2014 Adrian Chadd.  All rights reserved.
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

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/errno.h>
#include <sys/endian.h>

#include "uinet_api.h"
#include "uinet_config.h"
#include "sysctl_api.h"

#define	SYSCTL_BUF_LEN		131072
#define	SYSCTL_MAX_BUF_LEN	1048576
#define	SYSCTL_MAX_STR_LEN	1024
#define	SYSCTL_MAX_REQ_BUF_LEN	1048576

/*
 * Handle sysctl string type requests.
 *
 * Returns 1 if the connection should stay open; 0 if
 * not.
 */
static int
passive_sysctl_reqtype_str(int ns, char *buf, int len)
{
	struct sysctl_req_hdr *hdr;
	int retval = 0;
	char *sbuf = NULL;
	char *req_str = NULL;
	size_t wbuf_len = 0;
	size_t sbuf_len = 0;
	size_t rval;
	int error;
	struct sysctl_resp_hdr rhdr;
	char *wbuf = NULL;

	/* Request header; zero response header */
	hdr = (struct sysctl_req_hdr *) buf;
	bzero(&rhdr, sizeof(rhdr));

	/*
	 * Validate the various lengths.
	 */

	if (le32toh(hdr->sysctl_req_len) !=
	    le32toh(hdr->sysctl_str_len)
	    + le32toh(hdr->sysctl_src_len)
	    + sizeof(struct sysctl_req_hdr)) {
		fprintf(stderr, "%s: fd %d: length mismatch\n",
		    __func__,
		    ns);
		fprintf(stderr, "%s: fd %d: hdr_len=%d, req_len=%d, str_len=%d, src_len=%d\n",
		    __func__,
		    ns,
		    (int) sizeof(struct sysctl_req_hdr),
		    le32toh(hdr->sysctl_req_len),
		    le32toh(hdr->sysctl_str_len),
		    le32toh(hdr->sysctl_src_len));
		retval = 0;
		goto finish;
	}

	if (le32toh(hdr->sysctl_dst_len) > SYSCTL_MAX_BUF_LEN) {
		fprintf(stderr, "%s: fd %d: dst_len %d > %d\n",
		    __func__,
		    ns,
		    le32toh(hdr->sysctl_dst_len),
		    SYSCTL_MAX_BUF_LEN);
		retval = 0;
		goto finish;
	}

	/*
	 * Populate the request string.
	 */
	req_str = malloc(le32toh(hdr->sysctl_str_len) + 1);
	if (req_str == NULL) {
		fprintf(stderr, "%s; fd %d: malloc failed (req_str)\n",
		    __func__,
		    ns);
		retval = 0;
		goto finish;
	}

	memcpy(req_str, buf + sizeof(struct sysctl_req_hdr),
	    le32toh(hdr->sysctl_str_len));
	req_str[le32toh(hdr->sysctl_str_len)] = '\0';

	/*
	 * If there's a request buffer, populate that.
	 */
	if (le32toh(hdr->sysctl_src_len) > 0) {
		sbuf = buf + le32toh(hdr->sysctl_src_len);
		sbuf_len = le32toh(hdr->sysctl_src_len);
	}

	/*
	 * Allocate response buffer if requested.
	 */
	if (le32toh(hdr->sysctl_dst_len) > 0) {
		wbuf = malloc(le32toh(hdr->sysctl_dst_len));
		if (wbuf == NULL) {
			fprintf(stderr, "%s: fd %d: malloc failed: %d\n",
			    __func__,
			    ns,
			    errno);
			retval = 0;
			goto finish;
		}
		wbuf_len = le32toh(hdr->sysctl_dst_len);
	}

		/* Issue sysctl */
		fprintf(stderr,
		    "%s: fd %d: sysctl '%s' src_len=%d, dst_len=%d\n",
		    __func__,
		    ns,
		    req_str,
		    le32toh(hdr->sysctl_src_len),
		    le32toh(hdr->sysctl_dst_len));

		error = uinet_sysctl(req_str,
		    wbuf, &wbuf_len,
		    sbuf, sbuf_len,
		    &rval,
		    0);

		fprintf(stderr, "%s: fd %d: sysctl error=%d, wbuf_len=%d, rval=%d\n",
		    __func__,
		    ns,
		    (int) error,
		    (int) wbuf_len,
		    (int) rval);

		/*
		 * XXX Validate the response back from uinet_sysctl()
		 * is within bounds for the response back to the
		 * client.
		 */

		/* Construct our response */
		rhdr.sysctl_resp_len = htole32(sizeof(struct sysctl_resp_hdr) + wbuf_len);
		rhdr.sysctl_resp_type = 0; /* XXX */
		rhdr.sysctl_resp_flags = 0; /* XXX */

		if (errno == 0)
			rhdr.sysctl_dst_len = htole32(rval);
		else
			rhdr.sysctl_dst_len = 0;
		rhdr.sysctl_dst_errno = error;

		write(ns, &rhdr, sizeof(rhdr));
		if (wbuf_len > 0) {
			write(ns, wbuf, wbuf_len);
		}

		/* Done! */
		retval = 1;

finish:
	if (req_str != NULL)
		free(req_str);
	if (wbuf != NULL)
		free(wbuf);
	return (retval);
}

void *
passive_sysctl_listener(void *arg)
{
	int s, ns, r;
	struct sockaddr_un sun;
	char *rbuf;

	uinet_initialize_thread();

	rbuf = malloc(SYSCTL_BUF_LEN);
	if (rbuf == NULL) {
		printf("%s: malloc failed: %d\n", __func__, errno);
		return (NULL);
	}

	bzero(&sun, sizeof(sun));
	strcpy(sun.sun_path, "/tmp/sysctl.sock");
	sun.sun_len = 0;
	sun.sun_family = AF_UNIX;

	printf("sysctl_listener: starting listener on %s\n", sun.sun_path);
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		fprintf(stderr, "%s: socket failed: %d\n", __func__, errno);
		return NULL;
	}

	r = bind(s, (struct sockaddr *) &sun, sizeof(sun));
	if (r < 0) {
		fprintf(stderr, "%s: bind failed: %d\n", __func__, errno);
		return NULL;
	}

	r = listen(s, 10);
	if (r < 0) {
		fprintf(stderr, "%s: listen failed: %d\n", __func__, errno);
		return NULL;
	}

	/*
	 * Yes, I could make this threaded or non-blocking..
	 */
	for (;;) {
		struct sockaddr_un sun_n;
		socklen_t sl;
		int len;
		struct sysctl_req_hdr *hdr;
		int rlen = 0;

		ns = accept(s, (struct sockaddr *) &sun_n, &sl);
		if (ns < 0) {
			fprintf(stderr, "%s: accept failed: %d\n", __func__, errno);
			continue;
		}

		/* XXX I hate gotos */
readmore:
		/* Read data */
		len = read(ns, rbuf + rlen, SYSCTL_BUF_LEN - rlen);
		if (len <= 0) {
			fprintf(stderr, "%s: fd %d: read returned %d, errno=%d\n",
			    __func__,
			    ns,
			    len,
			    errno);
			goto next;
		}

		rlen += len;

		/*
		 * Not enough data? Keep reading.
		 */
		if (rlen < sizeof(struct sysctl_req_hdr)) {
			fprintf(stderr, "%s: fd %d: read %d btyes, rlen is now %d\n",
			    __func__,
			    ns,
			    len,
			    rlen);
			goto readmore;
		}

		hdr = (struct sysctl_req_hdr *) rbuf;

		/*
		 * Do we have enough data to cover the payload length?
		 */
		if (le32toh(hdr->sysctl_req_len) < rlen) {
			goto readmore;
		}

		/*
		 * We have the entire payload.  Let's dispatch based
		 * on type.
		 */
		(void) passive_sysctl_reqtype_str(ns, rbuf, rlen);

		/* XXX until we've taught the loop about
		 * how to consume readbuf data right and
		 * have the remainder data be moved to the
		 * head of the queue, let's just close it for
		 * now.
		 */
next:
		/* Close */
		close(ns);
	}

	return NULL;
}
