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

int
main(int argc, char *argv[])
{

	int s;
	struct sockaddr_un sun;
	int r;
	struct sysctl_req_hdr req_hdr;
	struct sysctl_resp_hdr *resp_hdr;
	char *req_buf, *resp_buf;
	char *req_str;
	size_t reqbuf_len, respbuf_len;

	/* Fake up a request structure for now */
	req_str = strdup("net.inet.tcp.stats");
	reqbuf_len = 0;
	respbuf_len = 1048576;
	req_buf = NULL;
	resp_buf = NULL;

	/* Ok, allocate req/resp buffers as required */
	if (reqbuf_len > 0) {
		req_buf = calloc(1, reqbuf_len);
		if (req_buf == NULL)
			err(1, "calloc");
	}

	if (respbuf_len > 0) {
		resp_buf = calloc(1, respbuf_len);
		if (resp_buf == NULL)
			err(1, "calloc");
	}

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

	/* Craft request header */
	bzero(&req_hdr, sizeof(req_hdr));
	req_hdr.sysctl_req_len = htole32(sizeof(req_hdr) + strlen(req_str) + reqbuf_len);
	req_hdr.sysctl_req_type = 0; /* XXX */
	req_hdr.sysctl_req_flags = 0; /* XXX */
	req_hdr.sysctl_str_len = htole32(strlen(req_str));
	req_hdr.sysctl_dst_len = htole32(respbuf_len);
	req_hdr.sysctl_src_len = htole32(reqbuf_len);

	/* Send request */
	r = write(s, &req_hdr, sizeof(req_hdr));
	if (r != sizeof(req_hdr)) {
		err(1, "write (hdr)");
	}
	r = write(s, req_str, strlen(req_str));
	if (r != strlen(req_str)) {
		err(1, "write (req_str)");
	}
	r = write(s, req_buf, reqbuf_len);
	if (r != reqbuf_len) {
		err(1, "write (req_buf)");
	}

	/* Read response */

	/* Done */
	close(s);

	exit(0);
}
