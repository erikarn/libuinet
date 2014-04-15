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

void *
passive_sysctl_listener(void *arg)
{
	int s, ns, r;
	struct sockaddr_un sun;
	char *rbuf, *wbuf = NULL;

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
		char *sbuf = NULL;
		char *req_str = NULL;
		size_t wbuf_len = 0;
		size_t sbuf_len = 0;
		size_t rval;
		int error;
		struct sysctl_resp_hdr rhdr;

		bzero(&rhdr, sizeof(rhdr));

		ns = accept(s, (struct sockaddr *) &sun_n, &sl);
		if (ns < 0) {
			fprintf(stderr, "%s: accept failed: %d\n", __func__, errno);
			continue;
		}

		/* Read data - assume we can get it all in one hit */
		len = read(ns, rbuf, SYSCTL_BUF_LEN);

		/*
		 * If the read is less than the request header, then we
		 * just turf it for now.
		 */
		if (len < sizeof(struct sysctl_req_hdr)) {
			fprintf(stderr, "%s: fd %d: len=%d, too short\n", __func__, ns, len);
			goto next;
		}

		hdr = (struct sysctl_req_hdr *) rbuf;

		/*
		 * Validate length fields and payload
		 *
		 * XXX TODO type, flags, strlen, srclen
		 */
		if (le32toh(hdr->sysctl_req_len) != len) {
			fprintf(stderr, "%s: fd %d: req_len (%d) != len (%d)\n",
			    __func__,
			    ns,
			    le32toh(hdr->sysctl_req_len),
			    len);
		}
		if (le32toh(hdr->sysctl_req_len) !=
		    le32toh(hdr->sysctl_str_len)
		    + le32toh(hdr->sysctl_src_len)
		    + le32toh(hdr->sysctl_dst_len)
		    + sizeof(struct sysctl_req_hdr)) {
			fprintf(stderr, "%s: fd %d: length mismatch\n",
			    __func__,
			    ns);
			goto next;
		}

		if (le32toh(hdr->sysctl_dst_len) > SYSCTL_MAX_BUF_LEN) {
			fprintf(stderr, "%s: fd %d: dst_len %d > %d\n",
			    __func__,
			    ns,
			    le32toh(hdr->sysctl_dst_len),
			    SYSCTL_MAX_BUF_LEN);
			goto next;
		}

		/*
		 * Populate the request string.
		 */
		req_str = malloc(le32toh(hdr->sysctl_str_len) + 1);
		if (req_str == NULL) {
			fprintf(stderr, "%s; fd %d: malloc failed (req_str)\n",
			    __func__,
			    ns);
			goto next;
		}
		memcpy(req_str, rbuf + sizeof(struct sysctl_req_hdr),
		    le32toh(hdr->sysctl_str_len));
		req_str[le32toh(hdr->sysctl_str_len)] = '\0';

		/*
		 * If there's a request buffer, populate that.
		 */
		if (le32toh(hdr->sysctl_src_len) > 0) {
			sbuf = rbuf + le32toh(hdr->sysctl_src_len);
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
				goto next;
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

#if 0
		/*
		 * XXX validate the response back from uinet_sysctl()
		 * is within bounds!
		 */

		/* Construct our response */
		rhdr.sysctl_resp_len = htole32(sizeof(struct sysctl_resp_hdr) + wbuf_len);
#endif

next:
		if (wbuf != NULL)
			free(wbuf);
		if (req_str != NULL)
			free(req_str);

		/* Close */
		close(ns);
	}

	return NULL;
}
