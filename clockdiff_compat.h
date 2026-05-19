/*
 * clockdiff_compat.h - macOS/BSD compatibility shims for clockdiff
 *
 * Provides Linux-compatible struct iphdr, struct icmphdr, and ppoll()
 * for non-Linux platforms.
 */
#ifndef CLOCKDIFF_COMPAT_H
#define CLOCKDIFF_COMPAT_H

#ifndef __linux__

#include <stdint.h>
#include <poll.h>
#include <netinet/in.h>

/* Linux-compatible IP header (little-endian bitfield order) */
struct iphdr {
#if __BYTE_ORDER == __LITTLE_ENDIAN
	uint8_t  ihl:4;
	uint8_t  version:4;
#else
	uint8_t  version:4;
	uint8_t  ihl:4;
#endif
	uint8_t  tos;
	uint16_t tot_len;
	uint16_t id;
	uint16_t frag_off;
	uint8_t  ttl;
	uint8_t  protocol;
	uint16_t check;
	uint32_t saddr;
	uint32_t daddr;
};

/* Linux-compatible ICMP header */
struct icmphdr {
	uint8_t  type;
	uint8_t  code;
	uint16_t checksum;
	union {
		struct {
			uint16_t id;
			uint16_t sequence;
		} echo;
		uint32_t gateway;
		struct {
			uint16_t mtu_unused;
			uint16_t mtu;
		} frag;
	} un;
};

/* ppoll is Linux-specific; emulate with poll */
#include <time.h>
static inline int ppoll(struct pollfd *fds, nfds_t nfds,
			const struct timespec *timeout,
			const void *sigmask __attribute__((unused)))
{
	int ms = -1;
	if (timeout)
		ms = (int)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
	return poll(fds, nfds, ms);
}

/* ICMP timestamp constants (from RFC 792) */
#ifndef ICMP_TIMESTAMP
# define ICMP_TIMESTAMP		13
#endif
#ifndef ICMP_TIMESTAMPREPLY
# define ICMP_TIMESTAMPREPLY	14
#endif

/* IP options constants */
#ifndef IPOPT_TIMESTAMP
# define IPOPT_TIMESTAMP	68	/* timestamp */
#endif
#ifndef IPOPT_TS_PRESPEC
# define IPOPT_TS_PRESPEC	3
#endif

#endif /* !__linux__ */

#endif /* CLOCKDIFF_COMPAT_H */
