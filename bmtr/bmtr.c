/*
 * bmtr.c — BrightSpace continuous per-hop probe (BrightMTR)
 *
 * Discovers the route to a destination, then continuously probes each hop
 * and displays rolling RTT statistics with BrightDate units and light-floor
 * per hop.  Refreshes the display in-place using ANSI escape codes.
 *
 * Usage:
 *   bmtr [options] <destination>
 *
 * Options:
 *   -c <count>       Stop after this many probe cycles (default: run forever)
 *   -i <interval>    Seconds between cycles (default: 2)
 *   -m <maxhops>     Max TTL / hops (default: 30)
 *   --report         Print one final report instead of live display
 *   --my-coord=lat,lon / --my-ecef=x,y,z   (CLI, goes in history)
 *
 * Location provider:
 *   BrightNexus bridge — BrightLink LINK_GEO_GET (RFC §9.4) via the
 *   'bsh-geo --get --format both --json' helper.  The bridge gates the
 *   request through the user's geo:precise ACL grant.
 */

#include "../brightspace.h"
#include "../brightlink_glue.h"
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define MAX_HOPS      30
#define MAX_SAMPLES  512    /* rolling window per hop */

static volatile int g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

typedef struct {
    int    num;
    char   host[80];
    char   ip[48];
    char   loc[48];
    bs_geo_t geo;

    /* Rolling statistics */
    double samples[MAX_SAMPLES];
    int    nsamp;
    int    ntimeout;
    int    nprobes;
    double sum, sumsq, smin, smax;
} bmtr_hop_t;

static void hop_add_sample(bmtr_hop_t *h, double rtt_ms)
{
    h->nprobes++;
    if (rtt_ms < 0.0) { h->ntimeout++; return; }
    int idx = h->nsamp % MAX_SAMPLES;
    h->samples[idx] = rtt_ms;
    h->nsamp++;
    h->sum   += rtt_ms;
    h->sumsq += rtt_ms * rtt_ms;
    if (h->nsamp == 1 || rtt_ms < h->smin) h->smin = rtt_ms;
    if (h->nsamp == 1 || rtt_ms > h->smax) h->smax = rtt_ms;
}

static double hop_avg(const bmtr_hop_t *h)
{
    return (h->nsamp > 0) ? h->sum / h->nsamp : 0.0;
}

static double hop_stddev(const bmtr_hop_t *h)
{
    if (h->nsamp < 2) return 0.0;
    double mean = hop_avg(h);
    double var  = h->sumsq / h->nsamp - mean * mean;
    return (var > 0.0) ? sqrt(var) : 0.0;
}

static int discover_hops(const char *dest, int maxhops,
                         bmtr_hop_t *hops, int *nhops_out)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "traceroute -q 1 -m %d %s 2>&1", maxhops, dest);
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    int n = 0;
    char buf[512];
    int first = 1;
    while (fgets(buf, sizeof(buf), fp) && n < maxhops) {
        if (first) { first = 0; continue; }
        buf[strcspn(buf, "\n")] = '\0';
        if (!buf[0]) continue;

        int hopnum = 0;
        sscanf(buf, " %d", &hopnum);
        if (hopnum <= 0) continue;

        bmtr_hop_t *h = &hops[n];
        memset(h, 0, sizeof(*h));
        h->num  = hopnum;
        h->smin = 1e9; h->smax = 0.0;
        bs_hop_host(buf, h->host, sizeof(h->host));
        bs_hop_ip(h->host, h->ip, sizeof(h->ip));
        n++;
    }
    pclose(fp);
    *nhops_out = n;
    return n > 0;
}

static void geolocate_hops(bmtr_hop_t *hops, int nhops)
{
    for (int i = 0; i < nhops; ++i) {
        bmtr_hop_t *h = &hops[i];
        if (strcmp(h->host, "* * *") == 0 || !h->ip[0]) continue;
        h->geo = bs_geolocate(h->ip);
        bs_geo_label(&h->geo, h->loc, sizeof(h->loc));
    }
}

static void probe_hop(bmtr_hop_t *h)
{
    if (!h->ip[0] || strcmp(h->host, "* * *") == 0) {
        hop_add_sample(h, -1.0);
        return;
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ping -c 1 -W 1 %s 2>&1", h->ip);
    FILE *fp = popen(cmd, "r");
    if (!fp) { hop_add_sample(h, -1.0); return; }

    double rtt = -1.0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        /* macOS: "round-trip min/avg/max/stddev = N/N/N/N ms" */
        const char *eq;
        if ((eq = strstr(buf, "=")) && strstr(buf, "min/avg/max")) {
            double vmin, vavg;
            if (sscanf(eq + 1, " %lf/%lf", &vmin, &vavg) >= 2)
                rtt = vavg;
        }
        /* also try "time=N.NNN ms" for single-packet output */
        const char *tp = strstr(buf, "time=");
        if (!tp) tp = strstr(buf, "time =");
        if (tp) {
            double t;
            if (sscanf(tp + (tp[4] == '=' ? 5 : 6), "%lf", &t) == 1)
                rtt = t;
        }
    }
    pclose(fp);
    hop_add_sample(h, rtt);
}

static void draw_table(const char *dest, const bmtr_hop_t *hops, int nhops,
                       int cycle, const bs_geo_t *my_geo)
{
    /* Move cursor to top-left (for in-place refresh after first draw) */
    printf("\033[H");

    printf("bmtr — %s    cycle %d    (Ctrl+C to stop)\n", dest, cycle);
    if (my_geo && my_geo->valid) {
        char lbl[80] = "", ecef[48];
        bs_geo_label(my_geo, lbl, sizeof(lbl));
        bs_geo_ecef_str(my_geo, ecef, sizeof(ecef));
        printf("src %-18s %s%s%s%s\n",
               my_geo->tag, ecef,
               lbl[0] ? "  (" : "", lbl, lbl[0] ? ")" : "");
    }
    printf("\n");
    printf("  %-4s  %-38s  %-20s  %6s  %8s  %8s  %8s  %10s  %10s\n",
           "hop", "host", "location",
           "loss%", "avg(ms)", "avg(md)", "stddev", "floor(mBM)", "floor(km)");
    printf("  %-4s  %-38s  %-20s  %6s  %8s  %8s  %8s  %10s  %10s\n",
           "----", "--------------------------------------",
           "--------------------",
           "------", "-------", "-------", "------", "----------", "----------");

    for (int i = 0; i < nhops; ++i) {
        const bmtr_hop_t *h = &hops[i];
        if (h->nprobes == 0) continue;

        double loss_pct = (h->nprobes > 0)
            ? (double)h->ntimeout / h->nprobes * 100.0 : 100.0;

        if (h->nsamp == 0) {
            printf("  %-4d  %-38s  %-20s  %5.1f%%  %8s  %8s  %8s  %10s  %10s\n",
                   h->num, h->host, h->loc, loss_pct,
                   "?", "?", "?", "?", "?");
        } else {
            double avg    = hop_avg(h);
            double stddev = hop_stddev(h);
            double floor_mbm = avg / 2.0;
            double floor_km  = bs_mbm_to_km(floor_mbm);
            printf("  %-4d  %-38s  %-20s  %5.1f%%  %8.3f  %8.6f  %8.3f  %10.4f  %10.1f\n",
                   h->num, h->host, h->loc, loss_pct,
                   avg, bs_to_md(avg), stddev,
                   floor_mbm, floor_km);
        }
    }
    printf("\033[J");  /* clear to end of screen (erase old lines on resize) */
    fflush(stdout);
}

static void draw_report(const char *dest, const bmtr_hop_t *hops, int nhops,
                        int cycles, const bs_geo_t *my_geo)
{
    printf("bmtr report — %s    %d cycles\n", dest, cycles);
    if (my_geo && my_geo->valid) {
        char lbl[80] = "", ecef[48];
        bs_geo_label(my_geo, lbl, sizeof(lbl));
        bs_geo_ecef_str(my_geo, ecef, sizeof(ecef));
        printf("src %-18s %s%s%s%s\n",
               my_geo->tag, ecef,
               lbl[0] ? "  (" : "", lbl, lbl[0] ? ")" : "");
    }
    printf("\n");
    printf("  %-4s  %-38s  %-20s  %6s  %8s  %8s  %8s  %8s  %10s\n",
           "hop", "host", "location",
           "loss%", "min(ms)", "avg(ms)", "max(ms)", "avg(md)", "floor(mBM)");
    printf("  %-4s  %-38s  %-20s  %6s  %8s  %8s  %8s  %8s  %10s\n",
           "----", "--------------------------------------",
           "--------------------",
           "------", "-------", "-------", "-------", "-------", "----------");

    for (int i = 0; i < nhops; ++i) {
        const bmtr_hop_t *h = &hops[i];
        double loss_pct = (h->nprobes > 0)
            ? (double)h->ntimeout / h->nprobes * 100.0 : 100.0;

        if (h->nsamp == 0) {
            printf("  %-4d  %-38s  %-20s  %5.1f%%  %8s  %8s  %8s  %8s  %10s\n",
                   h->num, h->host, h->loc, loss_pct, "?","?","?","?","?");
        } else {
            double avg      = hop_avg(h);
            double floor_mbm = avg / 2.0;
            printf("  %-4d  %-38s  %-20s  %5.1f%%  %8.3f  %8.3f  %8.3f  %8.6f  %10.4f\n",
                   h->num, h->host, h->loc, loss_pct,
                   h->smin, avg, h->smax, bs_to_md(avg), floor_mbm);
        }
    }
}

static void usage(void)
{
    fprintf(stderr,
        "\nUsage\n"
        "  bmtr [options] <destination>\n"
        "\nOptions:\n"
        "  -c <count>           Stop after N cycles (default: run forever)\n"
        "  -i <interval>        Seconds between cycles (default: 2)\n"
        "  -m <maxhops>         Max hops (default: 30)\n"
        "  --report             Print final report instead of live display\n"
        "  --reset-brightlink-pin Forget the BrightNexus TOFU pin and exit\n"
        "\nCoordinate flags (go into shell history):\n"
        "  --my-ecef=x,y,z      My ECEF position (BrightMeters)\n"
        "  --my-coord=lat,lon    My position (decimal degrees)\n"
        "\nLocation provider (BrightNexus / BrightLink):\n"
        "  bmtr shells out to 'bsh-geo --get --format both --json' to read the\n"
        "  current location from the BrightNexus bridge. The bridge gates the\n"
        "  request through the user's geo:precise ACL grant. No env vars are read.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    bl_glue_handle_global_args(argc, argv);

    bs_ecef_t my_ecef;
    memset(&my_ecef, 0, sizeof(my_ecef));
    int have_my_ecef = 0;

    bs_geo_t my_geo;
    memset(&my_geo, 0, sizeof(my_geo));

    int max_cycles = 0;   /* 0 = forever */
    int interval   = 2;
    int maxhops    = 30;
    int report_mode = 0;
    char *dest = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--my-ecef=", 10) == 0) {
            have_my_ecef = bs_parse_ecef(argv[i] + 10, &my_ecef);
        } else if (strncmp(argv[i], "--my-coord=", 11) == 0) {
            double lat, lon;
            if (bs_parse_latlon(argv[i] + 11, &lat, &lon)) {
                my_geo.lat = lat; my_geo.lon = lon; my_geo.valid = 1;
                snprintf(my_geo.tag, sizeof(my_geo.tag), "[coord]");
            }
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            max_cycles = atoi(argv[++i]);
        } else if (strncmp(argv[i], "-c", 2) == 0) {
            max_cycles = atoi(argv[i] + 2);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            interval = atoi(argv[++i]);
            if (interval < 1) interval = 1;
        } else if (strncmp(argv[i], "-i", 2) == 0) {
            interval = atoi(argv[i] + 2);
            if (interval < 1) interval = 1;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            maxhops = atoi(argv[++i]);
        } else if (strncmp(argv[i], "-m", 2) == 0) {
            maxhops = atoi(argv[i] + 2);
        } else if (strcmp(argv[i], "--report") == 0) {
            report_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0
                || strcmp(argv[i], "--help") == 0) {
            usage();
        } else if (argv[i][0] != '-') {
            dest = argv[i];
        }
    }
    if (!dest) usage();
    if (maxhops < 1 || maxhops > MAX_HOPS) maxhops = MAX_HOPS;

    bl_glue_get_geo(argv[0], &my_ecef, &have_my_ecef, &my_geo);
    if (!have_my_ecef && !my_geo.valid)
        my_geo = bs_geolocate(NULL);
    if (have_my_ecef && !my_geo.valid)
        bs_ecef_to_geo(my_ecef, &my_geo, "[ecef]");

    /* If brightlink missed, annotate the geoIP tag with the reason. */
    bl_glue_annotate_tag(&my_geo);

    /* Discover hops */
    fprintf(stderr, "Discovering route to %s ...\n", dest);
    bmtr_hop_t hops[MAX_HOPS];
    int nhops = 0;
    if (!discover_hops(dest, maxhops, hops, &nhops) || nhops == 0) {
        fprintf(stderr, "bmtr: could not reach %s\n", dest);
        return 1;
    }

    fprintf(stderr, "Geolocating %d hops ...\n", nhops);
    geolocate_hops(hops, nhops);

    signal(SIGINT, on_sigint);

    if (!report_mode) {
        /* Clear screen once, then use in-place refresh */
        printf("\033[2J");
    }

    int cycle = 0;
    while (!g_stop && (max_cycles == 0 || cycle < max_cycles)) {
        /* Probe each hop */
        for (int i = 0; i < nhops && !g_stop; ++i)
            probe_hop(&hops[i]);

        cycle++;

        if (!report_mode)
            draw_table(dest, hops, nhops, cycle, &my_geo);

        if (max_cycles == 0 || cycle < max_cycles) {
            for (int s = 0; s < interval && !g_stop; ++s)
                sleep(1);
        }
    }

    if (report_mode || g_stop) {
        if (!report_mode) printf("\n\n");
        draw_report(dest, hops, nhops, cycle, &my_geo);
    }

    return 0;
}
