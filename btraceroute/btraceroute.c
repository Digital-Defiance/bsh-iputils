/*
 * btraceroute.c — BrightSpace-aware traceroute
 *
 * Shows the geographic path to a destination: per-hop RTT in ms and millidays,
 * light-floor per hop, geoIP city/country, cumulative path distance, and a
 * summary comparing total geographic path length to direct great-circle distance.
 *
 * Coordinate source priority (same as all b* tools):
 *   --my-ecef=x,y,z / --my-coord=lat,lon   (CLI)
 *   BrightNexus bridge                     (BrightLink LINK_GEO_GET, §9.4
 *                                           of the BrightLink RFC, via bsh-geo)
 *   auto ip-api.com geoIP                   (fallback)
 */

#include "../brightspace.h"
#include "../brightlink_glue.h"
#include "../iputils_color.h"

#define MAX_HOPS 30

typedef struct {
    int    num;
    char   host[80];        /* "hostname (ip)" or "* * *" */
    char   ip[48];
    double avg_ms;          /* -1 if timeout */
    bs_geo_t geo;
} hop_t;

static void usage(void)
{
    fprintf(stderr,
        "\nUsage\n"
        "  btraceroute [options] <destination>\n"
        "\nCoordinate flags (go into shell history):\n"
        "  --my-ecef=x,y,z          My ECEF position (BrightMeters)\n"
        "  --my-coord=lat,lon        My position (decimal degrees)\n"
        "\nLocation provider (BrightNexus / BrightLink):\n"
        "  btraceroute shells out to 'bsh-geo --get --format both --json' to read\n"
        "  the current location from the BrightNexus bridge. The bridge gates the\n"
        "  request through the user's geo:precise ACL grant. No env vars are read.\n"
        "\nOptions:\n"
        "  -m <maxhops>              Maximum number of hops (default: 30)\n"
        "  -q <nqueries>             Queries per hop (default: 3)\n"
        "  --reset-brightlink-pin    Forget the BrightNexus TOFU pin and exit\n"
        IPU_COLOR_USAGE
        "  -h, --help                Show this help\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const ipu_colors_t *c;
    bl_glue_handle_global_args(argc, argv);
    ipu_color_init(&argc, argv);
    c = &ipu_colors;

    bs_ecef_t my_ecef;
    memset(&my_ecef, 0, sizeof(my_ecef));
    int have_my_ecef = 0;

    bs_geo_t my_geo;
    memset(&my_geo, 0, sizeof(my_geo));

    int maxhops  = 30;
    int nqueries = 3;
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
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            maxhops = atoi(argv[++i]);
            if (maxhops < 1 || maxhops > 255) maxhops = 30;
        } else if (strncmp(argv[i], "-m", 2) == 0) {
            maxhops = atoi(argv[i] + 2);
            if (maxhops < 1 || maxhops > 255) maxhops = 30;
        } else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            nqueries = atoi(argv[++i]);
            if (nqueries < 1 || nqueries > 10) nqueries = 3;
        } else if (strcmp(argv[i], "-h") == 0
                || strcmp(argv[i], "--help") == 0) {
            usage();
        } else if (argv[i][0] != '-') {
            dest = argv[i];
        }
    }
    if (!dest) usage();

    bl_glue_get_geo(argv[0], &my_ecef, &have_my_ecef, &my_geo);

    /* Resolve and geolocate the destination */
    char dest_ip[64] = "";
    bs_resolve_to_ip(dest, dest_ip, sizeof(dest_ip));
    bs_geo_t tgt_geo;
    memset(&tgt_geo, 0, sizeof(tgt_geo));
    if (dest_ip[0])
        tgt_geo = bs_geolocate(dest_ip);

    /* Auto-geolocate self if not provided */
    if (!have_my_ecef && !my_geo.valid)
        my_geo = bs_geolocate(NULL);
    if (have_my_ecef && !my_geo.valid)
        bs_ecef_to_geo(my_ecef, &my_geo, "[ecef]");

    /* If brightlink missed, annotate the geoIP tag with the reason. */
    bl_glue_annotate_tag(&my_geo);

    /* Print header */
    printf("%sbtraceroute to %s%s", IPU(c, title), dest, IPU(c, reset));
    if (dest_ip[0] && strcmp(dest_ip, dest) != 0)
        printf(" (%s)", dest_ip);
    if (tgt_geo.valid) {
        char lbl[96] = "";
        bs_geo_label(&tgt_geo, lbl, sizeof(lbl));
        if (lbl[0]) printf(", %s%s%s", IPU(c, location), lbl, IPU(c, reset));
        printf(" %s%s%s", IPU(c, tag), tgt_geo.tag, IPU(c, reset));
    }
    printf("\n");

    if (my_geo.valid) {
        char lbl[96] = "";
        bs_geo_label(&my_geo, lbl, sizeof(lbl));
        char ecef[48];
        bs_geo_ecef_str(&my_geo, ecef, sizeof(ecef));
        printf("src %s%-18s%s %s%s%s",
               IPU(c, tag), my_geo.tag, IPU(c, reset),
               IPU(c, value), ecef, IPU(c, reset));
        if (lbl[0])
            printf("  %s(%s)%s", IPU(c, location), lbl, IPU(c, reset));
        printf("\n");
    }

    /* Column header */
    printf("\n");
    printf("  %s%-4s%s  %s%-42s%s  %s%-22s%s  %s%-9s%s  %s%-11s%s  %s%-14s%s\n",
           IPU(c, header), "hop", IPU(c, reset),
           IPU(c, header), "host", IPU(c, reset),
           IPU(c, header), "location", IPU(c, reset),
           IPU(c, header), "rtt (ms)", IPU(c, reset),
           IPU(c, header), "rtt (md)", IPU(c, reset),
           IPU(c, header), "floor(mBM/km)", IPU(c, reset));
    printf("  %-4s  %-42s  %-22s  %9s  %11s  %14s\n",
           "----", "------------------------------------------",
           "----------------------",
           "---------", "-----------", "--------------");

    /* Run traceroute */
    char tr_cmd[512];
    snprintf(tr_cmd, sizeof(tr_cmd),
             "traceroute -q %d -m %d %s 2>&1", nqueries, maxhops, dest);
    FILE *fp = popen(tr_cmd, "r");
    if (!fp) { fprintf(stderr, "btraceroute: could not run traceroute\n"); return 1; }

    hop_t hops[MAX_HOPS];
    int nhops = 0;

    char buf[512];
    int first = 1;
    while (fgets(buf, sizeof(buf), fp) && nhops < MAX_HOPS) {
        if (first) { first = 0; continue; }
        buf[strcspn(buf, "\n")] = '\0';
        if (!buf[0]) continue;

        int hopnum = 0;
        sscanf(buf, " %d", &hopnum);
        if (hopnum <= 0) continue;

        hop_t *h = &hops[nhops];
        memset(h, 0, sizeof(*h));
        h->num    = hopnum;
        h->avg_ms = bs_line_avg_rtt(buf);
        bs_hop_host(buf, h->host, sizeof(h->host));
        bs_hop_ip(h->host, h->ip, sizeof(h->ip));

        char loc[48] = "";
        if (strcmp(h->host, "* * *") != 0 && h->ip[0]) {
            h->geo = bs_geolocate(h->ip);
            bs_geo_label(&h->geo, loc, sizeof(loc));
        }

        if (h->avg_ms < 0.0) {
            printf("  %s%-4d%s  %s%-42s%s  %s%-22s%s  %s%9s%s\n",
                   IPU(c, hop), h->num, IPU(c, reset),
                   IPU(c, host), h->host, IPU(c, reset),
                   IPU(c, location), loc, IPU(c, reset),
                   IPU(c, bad), "timeout", IPU(c, reset));
        } else {
            char floorbuf[20];
            snprintf(floorbuf, sizeof(floorbuf), "%.3f/~%.0f",
                     h->avg_ms / 2.0, h->avg_ms / 2.0 * BS_KM_PER_MBM);
            printf("  %s%-4d%s  %s%-42s%s  %s%-22s%s  %s%9.3f%s  %s%11.8f%s  %s%14s%s\n",
                   IPU(c, hop), h->num, IPU(c, reset),
                   IPU(c, host), h->host, IPU(c, reset),
                   IPU(c, location), loc, IPU(c, reset),
                   IPU(c, rtt), h->avg_ms, IPU(c, reset),
                   IPU(c, detail), bs_to_md(h->avg_ms), IPU(c, reset),
                   IPU(c, value), floorbuf, IPU(c, reset));
        }
        nhops++;
    }
    pclose(fp);

    /* ── Summary ────────────────────────────────────────── */
    printf("\n");

    /* Geographic path: sum of consecutive hop-to-hop Haversine distances */
    double path_km = 0.0;
    int    path_segs = 0;
    bs_geo_t *prev = my_geo.valid ? &my_geo : NULL;
    for (int i = 0; i < nhops; ++i) {
        if (!hops[i].geo.valid) continue;
        if (prev && prev->valid) {
            path_km += bs_haversine_km(prev->lat, prev->lon,
                                       hops[i].geo.lat, hops[i].geo.lon);
            path_segs++;
        }
        prev = &hops[i].geo;
    }

    /* Direct great-circle distance src → dest */
    double direct_km = 0.0;
    if (my_geo.valid && tgt_geo.valid)
        direct_km = bs_haversine_km(my_geo.lat, my_geo.lon,
                                    tgt_geo.lat, tgt_geo.lon);

    /* Light-floor from last reachable hop */
    double last_rtt_ms = 0.0;
    for (int i = nhops - 1; i >= 0; --i) {
        if (hops[i].avg_ms >= 0.0) { last_rtt_ms = hops[i].avg_ms; break; }
    }
    double floor_mbm = last_rtt_ms / 2.0;

    if (direct_km > 0.0) {
        double direct_mbm = bs_km_to_mbm(direct_km);
        printf("  ");
        ipu_fprint_label(stdout, c, c->label, "direct geo distance", 22);
        printf("   = %s%.3f%s %smBM%s  (%s~%.0f km%s)\n",
               IPU(c, value), direct_mbm, IPU(c, reset),
               IPU(c, unit), IPU(c, reset),
               IPU(c, detail), direct_km, IPU(c, reset));
        if (floor_mbm > 0.0) {
            double eff = (direct_mbm / floor_mbm) * 100.0;
            const char *estyle = ipu_efficiency_style(c, eff);
            printf("  ");
            ipu_fprint_label(stdout, c, c->label, "light-floor (last hop)", 22);
            printf("= %s%.3f%s %smBM%s  (%s~%.0f km%s)\n",
                   IPU(c, value), floor_mbm, IPU(c, reset),
                   IPU(c, unit), IPU(c, reset),
                   IPU(c, detail), bs_mbm_to_km(floor_mbm), IPU(c, reset));
            printf("  ");
            ipu_fprint_label(stdout, c, c->label, "efficiency", 22);
            printf("            = %s%.2f%%%s", estyle, eff, IPU(c, reset));
            if (eff > 100.0)
                printf("  %s[warn: exceeds c — geoIP may be off]%s",
                       IPU(c, warn), IPU(c, reset));
            printf("\n");
        }
    }
    if (path_km > 0.0 && path_segs > 1) {
        double ratio = (direct_km > 0.0) ? path_km / direct_km : 0.0;
        printf("  ");
        ipu_fprint_label(stdout, c, c->label, "geo path length", 22);
        printf("       = %s%.0f%s %skm%s  (%s%.2fx direct%s)\n",
               IPU(c, value), path_km, IPU(c, reset),
               IPU(c, unit), IPU(c, reset),
               IPU(c, detail), ratio, IPU(c, reset));
    }

    return 0;
}
