#ifndef	__SYSCTL_API_H__
#define	__SYSCTL_API_H__

typedef enum {
	SYSCTL_REQ_NONE = 0,
	SYSCTL_REQ_STR = 1,
	SYSCTL_REQ_OID = 2,
} sysctl_req_type_t;

#if 0
struct sysctl_req_hdr {
	uint32_t	sysctl_req_len;		/* length of the whole payload */
	uint32_t	sysctl_req_type;	/* Type of the message */
	uint32_t	sysctl_req_flags;	/* Message flags */

	/* This is the sysctl specific stuff */
	uint32_t	sysctl_str_len;
	uint32_t	sysctl_dst_len;		/* result (new) */
	uint32_t	sysctl_src_len;		/* request (old) */

	/* sysctl string follows, non-NUL terminated */

	/* srcbuf follows, if srclen != 0 */
};

struct sysctl_resp_hdr {
	uint32_t	sysctl_resp_len;
	uint32_t	sysctl_resp_type;
	uint32_t	sysctl_resp_flags;

	/* This is the sysctl specific stuff */
	uint32_t	sysctl_dst_len;		/* response buffer length */
	uint32_t	sysctl_dst_errno;	/* sysctl errno value */

	/* Response follows, if sysctl_dst_len != 0 */
};
#endif

extern	void * passive_sysctl_listener(void *arg);

#endif
