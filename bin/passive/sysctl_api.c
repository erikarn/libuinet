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
#include "nv.h"
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
passive_sysctl_reqtype_str(int ns, nvlist_t *nvl)
{
	struct sysctl_req_hdr *hdr;
	nvlist_t *nvl_resp = NULL;
	int retval = 0;
	char *wbuf = NULL;
	size_t wbuf_len = 0;
	size_t sbuf_len = 0;
	char *req_str = NULL;
	const char *sbuf;
	int error;
	size_t rval;

	/* Validate fields are here */
	if (! nvlist_exists_string(nvl, "sysctl_str")) {
		fprintf(stderr, "%s: fd %d: missing sysctl_str\n",
		    __func__,
		    ns);
		retval = 0;
		goto finish;
	}
	req_str = strdup(nvlist_get_string(nvl, "sysctl_str"));

	/* sysctl_respbuf_len */
	if (! nvlist_exists_number(nvl, "sysctl_respbuf_len")) {
		fprintf(stderr, "%s: fd %d: missing sysctl_respbuf_len\n",
		    __func__,
		    ns);
		retval = 0;
		goto finish;
	}
	if (nvlist_get_number(nvl, "sysctl_respbuf_len") > SYSCTL_MAX_REQ_BUF_LEN) {
		fprintf(stderr, "%s: fd %d: sysctl_respbuf_len is too big!\n",
		    __func__,
		    ns);
		retval = 0;
		goto finish;
	}
	wbuf_len = nvlist_get_number(nvl, "sysctl_respbuf_len");
	wbuf = calloc(1, wbuf_len);
	if (wbuf == NULL) {
		fprintf(stderr, "%s: fd %d: malloc failed\n", __func__, ns);
		retval = 0;
		goto finish;
	}

	/* sysctl_reqbuf */
	if (nvlist_exists_binary(nvl, "sysctl_reqbuf")) {
		sbuf = nvlist_get_binary(nvl, "sysctl_reqbuf", &sbuf_len);
	} else {
		sbuf = NULL;
		sbuf_len = 0;
	}

	/* Issue sysctl */
	fprintf(stderr,
	    "%s: fd %d: sysctl '%s' src_len=%d, dst_len=%d\n",
	    __func__,
	    ns,
	    req_str,
	    (int) sbuf_len,
	    (int) wbuf_len);

	/* XXX typecasting sbuf sucks */
	error = uinet_sysctlbyname(req_str,
	    wbuf, &wbuf_len,
	    (char *) sbuf, sbuf_len,
	    &rval,
	    0);

	fprintf(stderr, "%s: fd %d: sysctl error=%d, wbuf_len=%llu, rval=%llu\n",
	    __func__,
	    ns,
	    (int) error,
	    (unsigned long long) wbuf_len,
	    (unsigned long long) rval);

	/*
	 * XXX Validate the response back from uinet_sysctl()
	 * is within bounds for the response back to the
	 * client.
	 */
	if (error == 0 && rval >= wbuf_len) {
		fprintf(stderr, "%s: fd %d: rval (%llu) > wbuf_len (%llu)\n",
		    __func__,
		    ns,
		    (unsigned long long) rval,
		    (unsigned long long) wbuf_len);
		retval = 0;
		goto finish;
	}

	/* Construct our response */
	nvl_resp = nvlist_create(0);
	if (nvl_resp == NULL) {
		fprintf(stderr, "%s: fd %d: nvlist_create failed\n", __func__, ns);
		retval = 0;
		goto finish;
	}

	nvlist_add_number(nvl_resp, "sysctl_errno", error);
	if (error == 0) {
		nvlist_add_binary(nvl_resp, "sysctl_respbuf", wbuf, rval);
	}

	if (nvlist_send(ns, nvl_resp) < 0) {
		fprintf(stderr, "%s: fd %d: nvlist_send failed; errno=%d\n",
		    __func__,
		    ns,
		    errno);
		retval = 1;
		goto finish;
	}

	/* Done! */
	retval = 1;

finish:
	if (req_str != NULL)
		free(req_str);
	if (wbuf != NULL)
		free(wbuf);
	if (nvl_resp != NULL)
		nvlist_destroy(nvl_resp);
	return (retval);
}

/*
 * Handle sysctl oid type requests.
 *
 * Returns 1 if the connection should stay open; 0 if
 * not.
 *
 * XXX this is definitely not endian-clean.
 * I'm just passing in sysctl_oid as a binary array. Ew.
 */
static int
passive_sysctl_reqtype_oid(int ns, nvlist_t *nvl)
{
	struct sysctl_req_hdr *hdr;
	nvlist_t *nvl_resp = NULL;
	int retval = 0;
	char *wbuf = NULL;
	size_t wbuf_len = 0;
	size_t sbuf_len = 0;
	const int *req_oid = NULL;
	const char *sbuf;
	int error;
	size_t rval;
	size_t req_oid_len;

	/* Validate fields are here */
	if (! nvlist_exists_binary(nvl, "sysctl_oid")) {
		fprintf(stderr, "%s: fd %d: missing sysctl_oid\n",
		    __func__,
		    ns);
		retval = 0;
		goto finish;
	}
	req_oid = (const int *) nvlist_get_binary(nvl, "sysctl_oid",
	    &req_oid_len);
	if (req_oid_len % sizeof(int) != 0) {
		fprintf(stderr, "%s: fd %d: req_oid_len (%llu) is not a multiple of %d\n",
		    __func__,
		    ns,
		    (unsigned long long) req_oid_len,
		    (int) sizeof(int));
		retval = 0;
		goto finish;
	}

	/* sysctl_respbuf_len */
	if (! nvlist_exists_number(nvl, "sysctl_respbuf_len")) {
		fprintf(stderr, "%s: fd %d: missing sysctl_respbuf_len\n",
		    __func__,
		    ns);
		retval = 0;
		goto finish;
	}
	if (nvlist_get_number(nvl, "sysctl_respbuf_len") > SYSCTL_MAX_REQ_BUF_LEN) {
		fprintf(stderr, "%s: fd %d: sysctl_respbuf_len is too big!\n",
		    __func__,
		    ns);
		retval = 0;
		goto finish;
	}
	wbuf_len = nvlist_get_number(nvl, "sysctl_respbuf_len");
	if (wbuf_len == 0) {
		wbuf = NULL;
	} else {
		wbuf = calloc(1, wbuf_len);
		if (wbuf == NULL) {
			fprintf(stderr, "%s: fd %d: malloc failed\n", __func__, ns);
			retval = 0;
			goto finish;
		}
	}

	/* sysctl_reqbuf */
	if (nvlist_exists_binary(nvl, "sysctl_reqbuf")) {
		sbuf = nvlist_get_binary(nvl, "sysctl_reqbuf", &sbuf_len);
	} else {
		sbuf = NULL;
		sbuf_len = 0;
	}

	/* Issue sysctl */
	fprintf(stderr,
	    "%s: fd %d: sysctl oid oidlen=%d oldp=%p, oldplen=%d, newp=%p, newplen=%d\n",
	    __func__,
	    ns,
	    (int) (req_oid_len / sizeof(int)),
	    wbuf,
	    (int) wbuf_len,
	    sbuf,
	    (int) sbuf_len);

	/* XXX typecasting sbuf and req_oid sucks */
	error = uinet_sysctl((int *) req_oid, req_oid_len / sizeof(int),
	    wbuf, &wbuf_len,
	    (char *) sbuf, sbuf_len,
	    &rval,
	    0);

	fprintf(stderr, "%s: fd %d: sysctl error=%d, wbuf_len=%llu, rval=%llu\n",
	    __func__,
	    ns,
	    (int) error,
	    (unsigned long long) wbuf_len,
	    (unsigned long long) rval);


	/*
	 * We only copy the data back if wbuf is not NULL.
	 *
	 * The undocumented size lookup in sysctl is done by
	 * doing a sysctl fetch on the given OID but with oldplen=0 and
	 * oldp=NULL, oldplen gets updated with the storage size.
	 */
	/*
	 * XXX Validate the response back from uinet_sysctl()
	 * is within bounds for the response back to the
	 * client.
	 */
	if (wbuf != NULL && error == 0 && rval >= wbuf_len) {
		fprintf(stderr, "%s: fd %d: rval (%llu) > wbuf_len (%llu)\n",
		    __func__,
		    ns,
		    (unsigned long long) rval,
		    (unsigned long long) wbuf_len);
		retval = 0;
		goto finish;
	}

	/* Construct our response */
	nvl_resp = nvlist_create(0);
	if (nvl_resp == NULL) {
		fprintf(stderr, "%s: fd %d: nvlist_create failed\n", __func__, ns);
		retval = 0;
		goto finish;
	}

	nvlist_add_number(nvl_resp, "sysctl_errno", error);
	if (error == 0 && wbuf != NULL) {
		nvlist_add_binary(nvl_resp, "sysctl_respbuf", wbuf, rval);
	}
	nvlist_add_number(nvl_resp, "sysctl_respbuf_len", rval);

	if (nvlist_send(ns, nvl_resp) < 0) {
		fprintf(stderr, "%s: fd %d: nvlist_send failed; errno=%d\n",
		    __func__,
		    ns,
		    errno);
		retval = 1;
		goto finish;
	}

	/* Done! */
	retval = 1;

finish:
	if (wbuf != NULL)
		free(wbuf);
	if (nvl_resp != NULL)
		nvlist_destroy(nvl_resp);
	return (retval);
}

void *
passive_sysctl_listener(void *arg)
{
	int s, r;
	struct sockaddr_un sun;

	uinet_initialize_thread();

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
		nvlist_t *nvl;
		int ns;
		int ret;
		const char *type;

		ns = accept(s, (struct sockaddr *) &sun_n, &sl);
		if (ns < 0) {
			fprintf(stderr, "%s: accept failed: %d\n", __func__, errno);
			continue;
		}

		for (;;) {
			nvl = nvlist_recv(ns);
			if (nvl == NULL)
				break;

			if (! nvlist_exists_string(nvl, "type")) {
				fprintf(stderr, "%s: fd %d: no type; bailing\n",
				    __func__,
				    ns);
				break;
			}
			type = nvlist_get_string(nvl, "type");

			fprintf(stderr, "%s: fd %d: type=%s\n",
			    __func__,
			    ns,
			    type);

			/* Dispatch as appropriate */
			if (strncmp(type, "sysctl_str", 10) == 0) {
				ret = passive_sysctl_reqtype_str(ns, nvl);
			} else if (strncmp(type, "sysctl_oid", 10) == 0) {
				ret = passive_sysctl_reqtype_oid(ns, nvl);
			} else {
				fprintf(stderr, "%s: fd %d: unknown type=%s\n",
				    __func__,
				    ns,
				    nvlist_get_string(nvl, "type"));
				break;
			}

			/* Tidyup */
			nvlist_destroy(nvl);

			/* Ret == 0? Then we don't wait around */
			if (ret == 0)
				break;
		}

		/* Done; bail */
		close(ns);
	}

	return NULL;
}
