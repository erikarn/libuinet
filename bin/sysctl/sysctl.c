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
	char *req_buf, *read_buf;
	char *req_str;
	size_t reqbuf_len, respbuf_len;
	size_t readbuf_len;
	size_t read_ofs = 0;

	if (argc < 2) {
		printf("Usage: sysctl <sysctl string>\n");
		exit(127);
	}

	/* Fake up a request structure for now */
	req_str = strdup(argv[1]);
	reqbuf_len = 0;
	respbuf_len = 1048576;
	req_buf = NULL;
	read_buf = NULL;

	/* Ok, allocate request buffer */
	if (reqbuf_len > 0) {
		req_buf = calloc(1, reqbuf_len);
		if (req_buf == NULL)
			err(1, "calloc");
	}

	/*
	 * Calculate the readbuf_len.  It's the combination
	 * of the header size and the response payload.
	 */
	readbuf_len = respbuf_len + sizeof(struct sysctl_resp_hdr);

	/*
	 * Allocate the response buffer.  This includes the
	 * response header and response payload if required.
	 */
	read_buf = calloc(1, readbuf_len);
	if (read_buf == NULL)
		err(1, "calloc");

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
	while (read_ofs < readbuf_len) {
		/* Don't try to read if we have a full buffer */
		if (readbuf_len - read_ofs <= 0)
			break;
		r = read(s, read_buf + read_ofs, readbuf_len - read_ofs);
		if (r < 0) {
			err(1, "read");
		} if (r == 0) {
			fprintf(stderr, "%s: read early EOF\n", __func__);
			break;
		}

		read_ofs += r;

		/* if we don't have enough data for the header, continue */
		if (read_ofs < sizeof(struct sysctl_resp_hdr)) {
			continue;
		}

		/* Grab the response header */
		resp_hdr = (struct sysctl_resp_hdr *) read_buf;

		/*
		 * Is the response length greater than respbuf_len?
		 * Then the response is too large.  Naughty server.
		 */
		if (le32toh(resp_hdr->sysctl_resp_len) >
		    respbuf_len + sizeof(struct sysctl_resp_hdr)) {
			fprintf(stderr, "%s: resp_len (%d) is too long!\n",
			    __func__,
			    le32toh(resp_hdr->sysctl_resp_len));
			break;
		}

		/* Do we have enough data to match the response length? */
		if (read_ofs < le32toh(resp_hdr->sysctl_resp_len))
			continue;

		/* We have enough data - woo! More sanity checks! */

		/*
		 * Does the response buffer length exceed what we allocated?
		 * again, too big a response; bad coder.
		 */

		if (le32toh(resp_hdr->sysctl_dst_len) > respbuf_len) {
			fprintf(stderr, "%s: dst_len (%d) is too long!\n",
			    __func__,
			    le32toh(resp_hdr->sysctl_dst_len));
			break;
		}
		printf("%s: received response: errno=%d, %d bytes\n",
		    __func__,
		    le32toh(resp_hdr->sysctl_dst_errno),
		    le32toh(resp_hdr->sysctl_dst_len));
	}

	/* Done */
	close(s);

	exit(0);
}
