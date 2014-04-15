#ifndef	__SYSCTL_API_H__
#define	__SYSCTL_API_H__

struct sysctl_req_hdr {
	uint32_t	sysctl_str_len;
	uint32_t	sysctl_dst_len;
	uint32_t	sysctl_src_len;
};

extern	void * passive_sysctl_listener(void *arg);

#endif
