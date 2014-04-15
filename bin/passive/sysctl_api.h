#ifndef	__SYSCTL_API_H__
#define	__SYSCTL_API_H__

struct sysctl_req_hdr {
	uint32_t	sysctl_req_len;		/* length of the whole payload */
	uint32_t	sysctl_req_type;	/* Type of the message */
	uint32_t	sysctl_req_flags;	/* Message flags */

	/* This is the sysctl specific stuff */
	uint32_t	sysctl_str_len;
	uint32_t	sysctl_dst_len;		/* result (new) */
	uint32_t	sysctl_src_len;		/* request (old) */
};

struct sysctl_resp_hdr {
	uint32_t	sysctl_resp_len;
	uint32_t	sysctl_resp_type;

	/* This is the sysctl specific stuff */
	uint32_t	sysctl_dst_len;		/* response buffer length */
	uint32_t	sysctl_dst_errno;	/* sysctl errno value */
};

extern	void * passive_sysctl_listener(void *arg);

#endif
