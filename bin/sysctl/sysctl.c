#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <strings.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/endian.h>

#include "sysctl_api.h"
#include "nv.h"

static int
u_sysctlbyname(int ns,
    const char *name,
    void *oldp,
    size_t *oldlenp,
    const void *newp,
    size_t newlen)
{
	nvlist_t *nvl, *nvl_resp;
	int retval = 0;
	const char *rbuf;
	size_t r_len;
	int r_errno;

	/* Create nvlist to populate the request into */
	nvl = nvlist_create(0);
	if (nvl == NULL) {
		warn("nvlist_create");
		retval = -1;
		goto done;
	}

	/* Create nvlist for a sysctl_str request */
	nvlist_add_string(nvl, "type", "sysctl_str");
	nvlist_add_string(nvl, "sysctl_str", name);
	nvlist_add_number(nvl, "sysctl_respbuf_len", *oldlenp);
	if (newlen > 0) {
		nvlist_add_binary(nvl, "sysctl_reqbuf", newp, newlen);
	}

	/* Send command */
	if (nvlist_send(ns, nvl) < 0) {
		warn("nvlist_send");
		retval = -1;
		goto done;
	}

	/* Read response */
	nvl_resp = nvlist_recv(ns);
	if (nvl_resp == NULL) {
		warn("nvlist_recv");
		retval = -1;
		goto done;
	}

	if (! nvlist_exists_number(nvl_resp, "sysctl_errno")) {
		fprintf(stderr, "response: no errno?\n");
		goto done;
	}
	r_errno = (int) nvlist_get_number(nvl_resp, "sysctl_errno");

	/* XXX validate r_len versus oldlenp */
	if (nvlist_exists_binary(nvl_resp, "sysctl_respbuf")) {
		rbuf = nvlist_get_binary(nvl_resp, "sysctl_respbuf", &r_len);
		memcpy(oldp, rbuf, r_len);
		*oldlenp = r_len;
	} else {
		r_len = 0;
	}

	retval = 0;
	/* XXX */
	errno = r_errno;

done:
	if (nvl)
		nvlist_destroy(nvl);
	if (nvl_resp)
		nvlist_destroy(nvl_resp);
	return (retval);
}

static int
u_sysctl(int ns,
    int *oid,
    u_int namelen,
    void *oldp,
    size_t *oldlenp,
    const void *newp,
    size_t newlen)
{
	nvlist_t *nvl, *nvl_resp;
	int retval = 0;
	const char *rbuf;
	size_t r_len;
	int r_errno;

	/* Create nvlist to populate the request into */
	nvl = nvlist_create(0);
	if (nvl == NULL) {
		warn("nvlist_create");
		retval = -1;
		goto done;
	}

	/* Create nvlist for a sysctl_oid request */
	nvlist_add_string(nvl, "type", "sysctl_oid");
	nvlist_add_binary(nvl, "sysctl_oid", oid, namelen * sizeof(int));
	nvlist_add_number(nvl, "sysctl_respbuf_len", *oldlenp);
	if (newlen > 0) {
		nvlist_add_binary(nvl, "sysctl_reqbuf", newp, newlen);
	}

	/* Send command */
	if (nvlist_send(ns, nvl) < 0) {
		warn("nvlist_send");
		retval = -1;
		goto done;
	}

	/* Read response */
	nvl_resp = nvlist_recv(ns);
	if (nvl_resp == NULL) {
		warn("nvlist_recv");
		retval = -1;
		goto done;
	}

	if (! nvlist_exists_number(nvl_resp, "sysctl_errno")) {
		fprintf(stderr, "response: no errno?\n");
		goto done;
	}
	r_errno = (int) nvlist_get_number(nvl_resp, "sysctl_errno");

	/* XXX validate r_len versus oldlenp */
	if (nvlist_exists_binary(nvl_resp, "sysctl_respbuf")) {
		rbuf = nvlist_get_binary(nvl_resp, "sysctl_respbuf", &r_len);
		memcpy(oldp, rbuf, r_len);
		*oldlenp = r_len;
	} else {
		r_len = 0;
	}

	retval = 0;
	/* XXX */
	errno = r_errno;

done:
	if (nvl)
		nvlist_destroy(nvl);
	if (nvl_resp)
		nvlist_destroy(nvl_resp);
	return (retval);
}


int
main(int argc, char *argv[])
{

	int s;
	struct sockaddr_un sun;
	int r;
	size_t reqbuf_len = 0, respbuf_len = 0;
	char *req_str;
	char *req_buf = NULL;
	char *resp_buf;
	size_t r_len;

	if (argc < 2) {
		printf("Usage: sysctl <sysctl string>\n");
		exit(127);
	}

	/* Fake up a request structure for now */
	req_str = strdup(argv[1]);
	reqbuf_len = 0;
	respbuf_len = 1048576;

	/* XXX Reqbuf when required */

	/* Connect to the destination socket */
	bzero(&sun, sizeof(sun));

	strcpy(sun.sun_path, "/tmp/sysctl.sock");
	sun.sun_len = 0;
	sun.sun_family = AF_UNIX;

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		err(1, "socket");
	}

	r = connect(s, (struct sockaddr *) &sun, sizeof(struct sockaddr_un));
	if (r < 0) {
		err(1, "connect");
	}

	resp_buf = calloc(1, respbuf_len);
	if (resp_buf == NULL)
		err(1, "calloc");

#if 0
	/* Do a sysctl */
	r = u_sysctlbyname(s, req_str, resp_buf, &respbuf_len,
	    NULL, 0);
	printf("%s: str=%s, r=%d, errno=%d, len=%d\n",
	    __func__,
	    req_str,
	    r,
	    errno,
	    (int) respbuf_len);
#else
	/* Do a sysctl */
	int oida[2];
	oida[0] = 1;
	oida[1] = 6;
	r = u_sysctl(s, oida, 2, resp_buf, &respbuf_len,
	    NULL, 0);
	printf("%s: str=%s, r=%d, errno=%d, len=%d\n",
	    __func__,
	    req_str,
	    r,
	    errno,
	    (int) respbuf_len);
#endif

	/* Done */
	if (req_str)
		free(req_str);

done:

	/* Done with socket */
	close(s);

	exit(0);
}
