/*
 * clockdiff.h - shared declarations for clockdiff and bclockdiff
 */
#ifndef CLOCKDIFF_H
#define CLOCKDIFF_H

#include <stdint.h>
#include <netinet/in.h>
#include <time.h>

#include "clockdiff_compat.h"

enum {
	RANGE = 1,		/* best expected round-trip time, ms */
	MSGS = 50,
	TRIALS = 10,

	GOOD = 0,
	UNREACHABLE = 2,
	NONSTDTIME = 3,
	BREAK = 4,
	CONTINUE = 5,
	HOSTDOWN = 0x7fffffff,

	BIASP = 43199999,
	BIASN = -43200000,
	MODULO =  86400000,
	PROCESSING_TIME = 0,	/* ms. to reduce error in measurement */

	PACKET_IN = 1024
};

enum {
	time_format_ctime,
	time_format_iso
};

struct run_state {
	int interactive;
	uint16_t id;
	int sock_raw;
	struct sockaddr_in server;
	int ip_opt_len;
	int measure_delta;
	int measure_delta1;
	unsigned short seqno;
	unsigned short seqno0;
	unsigned short acked;
	long rtt;
	long min_rtt;
	long rtt_sigma;
	char *hisname;
	int time_format;
};

int measure(struct run_state *ctl);
void drop_rights(void);

#endif /* CLOCKDIFF_H */
