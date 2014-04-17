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

int
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

int
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

#if 0
	printf("sysctl: nl=%d, oldp=%p, oldlen=%d, newp=%p, newlen=%d\n",
	    namelen,
	    oldp,
	    (int) *oldlenp,
	    newp,
	    (int) newlen);
#endif

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
	} else if (nvlist_exists_number(nvl_resp, "sysctl_respbuf_len")) {
		r_len = nvlist_get_number(nvl_resp, "sysctl_respbuf_len");
	} else {
		r_len = 0;
	}
	*oldlenp = r_len;

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
u_sysctl_open(void)
{
	int s;
	struct sockaddr_un sun;
	int r;

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

	return (s);
}
