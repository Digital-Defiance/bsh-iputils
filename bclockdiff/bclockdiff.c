/*
 * bclockdiff.c - BrightDate-enabled clockdiff
 *
 * Copyright (c) 2026 BrightChain Contributors
 * Based on clockdiff.c from iputils
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../clockdiff.h"
#include "../iputils_common.h"

/* BrightDate output support */

#define BRIGHTDATE_DAY_MS       86400000.0
#define BRIGHTDATE_MICRODAY_MS  0.0864
#define BRIGHTDATE_NANODAY_MS   0.0000864

enum brightdate_unit {
	BD_UNIT_DAY,
	BD_UNIT_MICRODAY,
	BD_UNIT_NANODAY
};

struct bright_opts {
	int use_brightdate;
	enum brightdate_unit unit;
};

static double ms_to_brightdate(double ms, enum brightdate_unit unit)
{
	switch (unit) {
	case BD_UNIT_DAY:       return ms / BRIGHTDATE_DAY_MS;
	case BD_UNIT_MICRODAY:  return ms / BRIGHTDATE_MICRODAY_MS;
	case BD_UNIT_NANODAY:   return ms / BRIGHTDATE_NANODAY_MS;
	default:                return ms / BRIGHTDATE_DAY_MS;
	}
}

static const char *brightdate_unit_str(enum brightdate_unit unit)
{
	switch (unit) {
	case BD_UNIT_DAY:       return "d";
	case BD_UNIT_MICRODAY:  return "ud";
	case BD_UNIT_NANODAY:   return "nd";
	default:                return "d";
	}
}

static void usage(int exit_status)
{
	drop_rights();
	fprintf(stderr, "%s",
		"\nUsage:\n"
		"  bclockdiff [options] <destination>\n"
		"\nOptions:\n"
		"                without -o, use icmp timestamp only (see RFC0792, page 16)\n"
		"  -o            use IP timestamp and icmp echo\n"
		"  -o1           use three-term IP timestamp and icmp echo\n"
		"  -T, --time-format <ctime|iso>\n"
		"                  specify display time format, ctime is the default\n"
		"  -I            alias of --time-format=iso\n"
		"  -B, --brightdate         output also in BrightDate units\n"
		"  --unit=<d|ud|nd>        select BrightDate output unit (default: ud)\n"
		"  -h, --help    display this help\n"
		"  -V, --version print version and exit\n"
		"  <destination> DNS name or IP address\n"
		"\nFor more details see bclockdiff(8).\n");
	exit(exit_status);
}

static void parse_opts(struct run_state *ctl, struct bright_opts *bopts,
		       int argc, char **argv)
{
	static const struct option longopts[] = {
		{"time-format", required_argument, NULL, 'T'},
		{"version",     no_argument,       NULL, 'V'},
		{"help",        no_argument,       NULL, 'h'},
		{"brightdate",  no_argument,       NULL, 'B'},
		{"unit",        required_argument, NULL, 1000},
		{NULL, 0, NULL, 0}
	};
	int c;

	bopts->use_brightdate = 0;
	bopts->unit = BD_UNIT_MICRODAY;

	while ((c = getopt_long(argc, argv, "o1T:IVhB", longopts, NULL)) != -1) {
		switch (c) {
		case 'o':
			ctl->ip_opt_len = 4 + 4 * 8;
			break;
		case '1':
			ctl->ip_opt_len = 4 + 3 * 8;
			break;
		case 'T':
			if (!strcmp(optarg, "iso"))
				ctl->time_format = time_format_iso;
			else if (!strcmp(optarg, "ctime"))
				ctl->time_format = time_format_ctime;
			else
				error(1, 0, "invalid time-format argument: %s", optarg);
			break;
		case 'I':
			ctl->time_format = time_format_iso;
			break;
		case 'B':
			bopts->use_brightdate = 1;
			break;
		case 1000:
			if (!strcmp(optarg, "d"))
				bopts->unit = BD_UNIT_DAY;
			else if (!strcmp(optarg, "ud"))
				bopts->unit = BD_UNIT_MICRODAY;
			else if (!strcmp(optarg, "nd"))
				bopts->unit = BD_UNIT_NANODAY;
			else
				error(1, 0, "invalid unit: %s", optarg);
			break;
		case 'V':
			printf(IPUTILS_VERSION("bclockdiff"));
			print_config();
			exit(0);
		case 'h':
			usage(0);
			abort();
		default:
			fprintf(stderr, "Try 'bclockdiff --help' for more information.\n");
			exit(1);
		}
	}
}

int main(int argc, char **argv)
{
	struct run_state ctl = {
		.rtt = 1000,
		.time_format = time_format_ctime
	};
	struct bright_opts bopts = {0};
	int measure_status;

	struct addrinfo hints = {
		.ai_family   = AF_INET,
		.ai_socktype = SOCK_RAW,
		.ai_flags    = AI_CANONNAME
	};
	struct addrinfo *result;
	int status;

#ifdef ENABLE_NLS
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE_NAME, LOCALEDIR);
	textdomain(PACKAGE_NAME);
#endif

	atexit(close_stdout);

	parse_opts(&ctl, &bopts, argc, argv);
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage(1);

	ctl.sock_raw = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (ctl.sock_raw < 0)
		error(1, errno, "socket");
	if (nice(-16) == -1)
		error(1, errno, "nice");
	drop_rights();

	if (isatty(fileno(stdin)) && isatty(fileno(stdout)))
		ctl.interactive = 1;

	ctl.id = getpid();

	status = getaddrinfo(argv[0], NULL, &hints, &result);
	if (status)
		error(1, 0, "%s: %s", argv[0], gai_strerror(status));
	ctl.hisname = strdup(result->ai_canonname);

	memcpy(&ctl.server, result->ai_addr, sizeof ctl.server);
	freeaddrinfo(result);

	if (connect(ctl.sock_raw, (struct sockaddr *)&ctl.server,
		    sizeof(ctl.server)) == -1)
		error(1, errno, "connect");

	if (ctl.ip_opt_len) {
		struct sockaddr_in myaddr = {0};
		socklen_t addrlen = sizeof(myaddr);
		uint8_t *rspace;

		if ((rspace = calloc(ctl.ip_opt_len, sizeof(uint8_t))) == NULL)
			error(1, errno, "allocating %zu bytes failed",
			      ctl.ip_opt_len * sizeof(uint8_t));
		rspace[0] = IPOPT_TIMESTAMP;
		rspace[1] = ctl.ip_opt_len;
		rspace[2] = 5;
		rspace[3] = IPOPT_TS_PRESPEC;
		if (getsockname(ctl.sock_raw, (struct sockaddr *)&myaddr,
				&addrlen) == -1)
			error(1, errno, "getsockname");
		((uint32_t *)(rspace + 4))[0 * 2] = myaddr.sin_addr.s_addr;
		((uint32_t *)(rspace + 4))[1 * 2] = ctl.server.sin_addr.s_addr;
		((uint32_t *)(rspace + 4))[2 * 2] = myaddr.sin_addr.s_addr;
		if (ctl.ip_opt_len == 4 + 4 * 8) {
			((uint32_t *)(rspace + 4))[2 * 2] = ctl.server.sin_addr.s_addr;
			((uint32_t *)(rspace + 4))[3 * 2] = myaddr.sin_addr.s_addr;
		}
		if (setsockopt(ctl.sock_raw, IPPROTO_IP, IP_OPTIONS,
			       rspace, ctl.ip_opt_len) < 0) {
			error(0, errno, "IP_OPTIONS (fallback to icmp tstamps)");
			ctl.ip_opt_len = 0;
		}
		free(rspace);
	}

	measure_status = measure(&ctl);
	if (measure_status < 0) {
		if (errno)
			error(1, errno, "measure");
		error(1, 0, "measure: unknown failure");
	}

	switch (measure_status) {
	case HOSTDOWN:
		error(1, 0, "%s is down", ctl.hisname);
		break;
	case NONSTDTIME:
		error(1, 0, "%s time transmitted in a non-standard format",
		      ctl.hisname);
		break;
	case UNREACHABLE:
		error(1, 0, "%s is unreachable", ctl.hisname);
		break;
	default:
		break;
	}

	{
		time_t now = time(NULL);

		if (ctl.interactive) {
			char s[32];
			struct tm tm;

			localtime_r(&now, &tm);
			if (ctl.time_format == time_format_iso)
				strftime(s, sizeof(s), "%Y-%m-%dT%H:%M:%S%z", &tm);
			else
				strftime(s, sizeof(s), "%a %b %e %H:%M:%S %Y", &tm);

			printf("\nhost=%s rtt=%ld(%ld)ms/%ldms delta=%dms/%dms %s\n",
			       ctl.hisname, ctl.rtt, ctl.rtt_sigma, ctl.min_rtt,
			       ctl.measure_delta, ctl.measure_delta1, s);
			if (bopts.use_brightdate) {
				printf("BrightDate: rtt=%.9f%s delta=%.9f%s\n",
				       ms_to_brightdate(ctl.rtt, bopts.unit),
				       brightdate_unit_str(bopts.unit),
				       ms_to_brightdate(ctl.measure_delta, bopts.unit),
				       brightdate_unit_str(bopts.unit));
			}
		} else {
			printf("%lld %d %d",
			       (long long)now,
			       ctl.measure_delta,
			       ctl.measure_delta1);
			if (bopts.use_brightdate) {
				printf(" %.9f%s %.9f%s",
				       ms_to_brightdate(ctl.rtt, bopts.unit),
				       brightdate_unit_str(bopts.unit),
				       ms_to_brightdate(ctl.measure_delta, bopts.unit),
				       brightdate_unit_str(bopts.unit));
			}
			printf("\n");
		}
	}
	exit(0);
}
