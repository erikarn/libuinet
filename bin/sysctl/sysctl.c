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
main(int argc, char *argv[])
{

	int s;
	struct sockaddr_un sun;
	int r;
	nvlist_t *nvl, *nvl_resp;
	size_t reqbuf_len = 0, respbuf_len = 0;
	char *req_str;
	char *req_buf = NULL;
	const char *resp_buf;
	size_t r_len;
	int r_errno;

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

	/* Create nvlist to populate the request into */
	nvl = nvlist_create(0);
	if (nvl == NULL)
		err(1, "nvlist_create");

	/* Create nvlist for a sysctl_str request */
	nvlist_add_string(nvl, "type", "sysctl_str");
	nvlist_add_string(nvl, "sysctl_str", req_str);
	nvlist_add_number(nvl, "sysctl_respbuf_len", respbuf_len);
	if (reqbuf_len > 0) {
		nvlist_add_binary(nvl, "sysctl_reqbuf", req_buf, reqbuf_len);
	}

	/* Send command */
	if (nvlist_send(s, nvl) < 0) {
		err(1, "nvlist_send");
	}

	/* Read response */
	nvl_resp = nvlist_recv(s);
	if (nvl_resp == NULL) {
		err(1, "nvlist_recv");
	}

	if (! nvlist_exists_number(nvl_resp, "sysctl_errno")) {
		fprintf(stderr, "response: no errno?\n");
		goto done;
	}
	r_errno = (int) nvlist_get_number(nvl_resp, "sysctl_errno");

	if (nvlist_exists_binary(nvl_resp, "sysctl_respbuf")) {
		resp_buf = nvlist_get_binary(nvl_resp, "sysctl_respbuf", &r_len);
	} else {
		r_len = 0;
	}

	printf("%s: str=%s, errno=%d, len=%d\n",
	    __func__,
	    req_str,
	    (int) r_errno,
	    (int) r_len);


done:
	/* Done with request/response */
	nvlist_destroy(nvl);
	nvlist_destroy(nvl_resp);

	/* Done with socket */
	close(s);

	exit(0);
}
