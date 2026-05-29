/*
 * clockdiff_compat.h - macOS/BSD compatibility shims for clockdiff
 *
 * Provides Linux-compatible struct iphdr, struct icmphdr, and ppoll()
 * for non-Linux platforms.
 */
#ifndef CLOCKDIFF_COMPAT_H
#define CLOCKDIFF_COMPAT_H

#include <stdint.h>

#if defined(__linux__)
# define CLOCKDIFF_RECV_INCLUDES_IP 1
# define CLOCKDIFF_ICMP_ID_NET_ORDER 0
#else
/*
 * BSD/Darwin raw ICMP sockets deliver the IPv4 header (see icmp(4)).
 * ICMP id/sequence fields remain in network byte order on the wire.
 */
# define CLOCKDIFF_RECV_INCLUDES_IP 1
# define CLOCKDIFF_ICMP_ID_NET_ORDER 1

# include <poll.h>
# include <arpa/inet.h>
# include <netinet/in.h>

/* Linux-compatible IP header (little-endian bitfield order) */
struct iphdr {
# if __BYTE_ORDER == __LITTLE_ENDIAN
	uint8_t  ihl:4;
	uint8_t  version:4;
# else
	uint8_t  version:4;
	uint8_t  ihl:4;
# endif
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
# include <time.h>
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
# ifndef ICMP_TIMESTAMP
#  define ICMP_TIMESTAMP	13
# endif
# ifndef ICMP_TIMESTAMPREPLY
#  define ICMP_TIMESTAMPREPLY	14
# endif

/* IP options constants */
# ifndef IPOPT_TIMESTAMP
#  define IPOPT_TIMESTAMP	68	/* timestamp */
# endif
# ifndef IPOPT_TS_PRESPEC
#  define IPOPT_TS_PRESPEC	3
# endif
#endif /* !__linux__ */

/*
 * Locate the ICMP header inside a received datagram.
 */
static inline struct icmphdr *clockdiff_packet_icmp(const unsigned char *packet,
						    int pkt_len, int *ip_hdr_len)
{
#if CLOCKDIFF_RECV_INCLUDES_IP
	const struct iphdr *ip = (const struct iphdr *)packet;
	int hlen;

	if (pkt_len < (int)sizeof(struct iphdr))
		return NULL;
	hlen = ip->ihl << 2;
	if (ip->ihl < 5 || hlen > pkt_len ||
	    pkt_len - hlen < (int)sizeof(struct icmphdr))
		return NULL;
	if (ip_hdr_len)
		*ip_hdr_len = hlen;
	return (struct icmphdr *)(packet + hlen);
#else
	if (pkt_len < (int)sizeof(struct icmphdr))
		return NULL;
	if (ip_hdr_len)
		*ip_hdr_len = 0;
	return (struct icmphdr *)packet;
#endif
}

static inline const unsigned char *clockdiff_ipopts(const unsigned char *packet,
						    int ip_hdr_len)
{
#if CLOCKDIFF_RECV_INCLUDES_IP
	return packet + ip_hdr_len;
#else
	(void)packet;
	(void)ip_hdr_len;
	return NULL;
#endif
}

static inline int clockdiff_icmp_octets(int pkt_len, int ip_hdr_len)
{
	int n = pkt_len - ip_hdr_len;

	return n >= 0 ? n : 0;
}

#if CLOCKDIFF_ICMP_ID_NET_ORDER
static inline uint16_t clockdiff_icmp_id(const struct icmphdr *icp)
{
	return ntohs(icp->un.echo.id);
}

static inline uint16_t clockdiff_icmp_seq(const struct icmphdr *icp)
{
	return ntohs(icp->un.echo.sequence);
}

static inline void clockdiff_icmp_set_idseq(struct icmphdr *icp, uint16_t id,
					    uint16_t seq)
{
	icp->un.echo.id = htons(id);
	icp->un.echo.sequence = htons(seq);
}
#else
static inline uint16_t clockdiff_icmp_id(const struct icmphdr *icp)
{
	return icp->un.echo.id;
}

static inline uint16_t clockdiff_icmp_seq(const struct icmphdr *icp)
{
	return icp->un.echo.sequence;
}

static inline void clockdiff_icmp_set_idseq(struct icmphdr *icp, uint16_t id,
					    uint16_t seq)
{
	icp->un.echo.id = id;
	icp->un.echo.sequence = seq;
}
#endif

#endif /* CLOCKDIFF_COMPAT_H */
