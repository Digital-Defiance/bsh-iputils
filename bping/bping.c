/*
 * bping.c  —  BrightChain physical network audit tool
 *
 * Coordinate source priority (highest → lowest):
 *   --my-ecef=x,y,z      ECEF in BrightMeters (CLI, audit-grade)   [ecef]
 *   --my-coord=lat,lon    Decimal degrees      (CLI, explicit)      [coord]
 *   BrightNexus bridge    BrightLink LINK_GEO_GET via 'bsh-geo'     [brightlink] /
 *                         (BrightLink RFC §9.4)                     [brightlink:ecef]
 *   auto ip-api.com lookup of public IP        (fallback)           [~geoIP]
 */

#include "../brightspace.h"
#include "../brightlink_glue.h"

static void print_stats(const char *dest,
                        double min_ms, double avg_ms,
                        double max_ms, double mdev_ms,
                        int have_ecef, double ecef_mbm,
                        const bs_geo_t *my_geo, const bs_geo_t *tgt_geo)
{
    double floor_mbm = avg_ms / 2.0;

    printf("bping %s:\n", dest);
    printf("  rtt min/avg/max/mdev  = %.3f/%.3f/%.3f/%.3f ms\n",
           min_ms, avg_ms, max_ms, mdev_ms);
    printf("  rtt min/avg/max/mdev  = %.8f/%.8f/%.8f/%.8f md\n",
           bs_to_md(min_ms), bs_to_md(avg_ms),
           bs_to_md(max_ms), bs_to_md(mdev_ms));
    printf("  light-floor (RTT/2)   = %.3f mBM  (~%.0f km)\n",
           floor_mbm, bs_mbm_to_km(floor_mbm));

    if (have_ecef) {
        double eff = (floor_mbm > 0.0) ? (ecef_mbm / floor_mbm) * 100.0 : 0.0;
        printf("  chord dist [ecef]     = %.3f mBM  (~%.0f km)\n",
               ecef_mbm, bs_mbm_to_km(ecef_mbm));
        printf("  light-spd limit [ecef]= %.3f ms   (%.8f md)\n",
               ecef_mbm, bs_to_md(ecef_mbm));
        if (eff > 100.0)
            printf("  efficiency [ecef]     = %.2f%%  [warn: exceeds c — check coords]\n", eff);
        else
            printf("  efficiency [ecef]     = %.2f%%\n", eff);
        return;
    }

    if (my_geo && my_geo->valid && tgt_geo && tgt_geo->valid) {
        double km   = bs_haversine_km(my_geo->lat, my_geo->lon,
                                      tgt_geo->lat, tgt_geo->lon);
        double dist = bs_km_to_mbm(km);
        double eff  = (floor_mbm > 0.0) ? (dist / floor_mbm) * 100.0 : 0.0;

        char src_loc[96] = "", tgt_loc[96] = "";
        bs_geo_label(my_geo,  src_loc, sizeof(src_loc));
        bs_geo_label(tgt_geo, tgt_loc, sizeof(tgt_loc));

        char src_ecef[48], tgt_ecef[48];
        bs_geo_ecef_str(my_geo,  src_ecef, sizeof(src_ecef));
        bs_geo_ecef_str(tgt_geo, tgt_ecef, sizeof(tgt_ecef));
        printf("  src %-18s  %s%s%s%s\n",
               my_geo->tag, src_ecef,
               src_loc[0] ? "  (" : "", src_loc, src_loc[0] ? ")" : "");
        printf("  tgt %-18s  %s%s%s%s\n",
               tgt_geo->tag, tgt_ecef,
               tgt_loc[0] ? "  (" : "", tgt_loc, tgt_loc[0] ? ")" : "");
        printf("  geo distance          = %.3f mBM  (~%.0f km)\n", dist, km);
        printf("  light-spd limit       = %.3f ms   (%.8f md)\n",
               dist, bs_to_md(dist));
        if (eff > 100.0)
            printf("  efficiency            = %.2f%%  [warn: exceeds c — geoIP may be off]\n", eff);
        else
            printf("  efficiency            = %.2f%%\n", eff);
    }
}

static void print_hops(const char *dest)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "traceroute -q 3 -m 30 %s 2>&1", dest);
    FILE *fp = popen(cmd, "r");
    if (!fp) { fprintf(stderr, "bping: could not run traceroute\n"); return; }

    printf("\nPer-hop trace:\n");
    printf("  %-4s  %-40s  %-20s  %10s  %12s  %16s\n",
           "hop", "host", "location",
           "rtt (ms)", "rtt (md)", "floor (mBM/km)");
    printf("  %-4s  %-40s  %-20s  %10s  %12s  %16s\n",
           "----", "----------------------------------------",
           "--------------------",
           "----------", "------------", "----------------");

    char buf[512];
    int first = 1;
    while (fgets(buf, sizeof(buf), fp)) {
        if (first) { first = 0; continue; }
        buf[strcspn(buf, "\n")] = '\0';
        if (!buf[0]) continue;

        int hop = 0;
        sscanf(buf, " %d", &hop);
        if (hop <= 0) continue;

        char host[64];
        bs_hop_host(buf, host, sizeof(host));
        double avg = bs_line_avg_rtt(buf);

        char loc[48] = "";
        if (strcmp(host, "* * *") != 0) {
            char ip[48] = "";
            bs_hop_ip(host, ip, sizeof(ip));
            if (ip[0]) {
                bs_geo_t g = bs_geolocate(ip);
                bs_geo_label(&g, loc, sizeof(loc));
            }
        }

        if (avg < 0.0) {
            printf("  %-4d  %-40s  %-20s  %10s\n", hop, host, loc, "timeout");
        } else {
            char floorbuf[24];
            snprintf(floorbuf, sizeof(floorbuf), "%.3f/~%.0f",
                     avg / 2.0, avg / 2.0 * BS_KM_PER_MBM);
            printf("  %-4d  %-40s  %-20s  %10.3f  %12.8f  %16s\n",
                   hop, host, loc, avg, bs_to_md(avg), floorbuf);
        }
    }
    pclose(fp);
}

static void usage(void)
{
    fprintf(stderr,
        "\nUsage\n"
        "  bping [options] <destination>\n"
        "\nCoordinate flags (CLI — highest priority, go into shell history):\n"
        "  --my-ecef=x,y,z          My ECEF position (BrightMeters, audit-grade)\n"
        "  --my-coord=lat,lon        My position (decimal degrees)\n"
        "  --target-ecef=x,y,z      Target ECEF (BrightMeters, audit-grade)\n"
        "  --target-coord=lat,lon    Target position (decimal degrees)\n"
        "\nLocation provider (BrightNexus / BrightLink):\n"
        "  bping shells out to 'bsh-geo --get --format both --json' to read the\n"
        "  current location from the BrightNexus bridge. The bridge gates the\n"
        "  request through the user's geo:precise ACL grant; the first call\n"
        "  prompts, subsequent calls succeed silently while the grant is live.\n"
        "  No environment variables are consulted.\n"
        "\nOther flags:\n"
        "  --hops                    Per-hop trace with BrightDate units + geoIP\n"
        "  -h, --help                Show this help\n"
        "\nCoordinate source priority:\n"
        "  --my-ecef > --my-coord > BrightNexus bridge > auto-geoIP\n"
        "  Target is always auto-geolocated unless an explicit flag is given.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    bs_ecef_t my_ecef, tgt_ecef;
    memset(&my_ecef,  0, sizeof(my_ecef));
    memset(&tgt_ecef, 0, sizeof(tgt_ecef));
    int have_my_ecef = 0, have_tgt_ecef = 0;

    bs_geo_t my_geo, tgt_geo;
    memset(&my_geo,  0, sizeof(my_geo));
    memset(&tgt_geo, 0, sizeof(tgt_geo));

    int show_hops = 0;
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
        } else if (strncmp(argv[i], "--target-ecef=", 14) == 0) {
            have_tgt_ecef = bs_parse_ecef(argv[i] + 14, &tgt_ecef);
        } else if (strncmp(argv[i], "--target-coord=", 15) == 0) {
            double lat, lon;
            if (bs_parse_latlon(argv[i] + 15, &lat, &lon)) {
                tgt_geo.lat = lat; tgt_geo.lon = lon; tgt_geo.valid = 1;
                snprintf(tgt_geo.tag, sizeof(tgt_geo.tag), "[coord]");
            }
        } else if (strcmp(argv[i], "--hops") == 0) {
            show_hops = 1;
        } else if (strcmp(argv[i], "-h") == 0
                || strcmp(argv[i], "--help") == 0) {
            usage();
        } else if (argv[i][0] != '-') {
            dest = argv[i];
        }
    }
    if (!dest) usage();

    bl_glue_get_geo(argv[0], &my_ecef, &have_my_ecef, &my_geo);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ping -c 3 %s 2>&1", dest);
    FILE *fp = popen(cmd, "r");
    if (!fp) { fprintf(stderr, "bping: could not run ping\n"); return 1; }

    double min_ms = 0, avg_ms = 0, max_ms = 0, mdev_ms = 0;
    int got = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        const char *eq;
        if ((eq = strstr(buf, "=")) && strstr(buf, "min/avg/max"))
            if (sscanf(eq + 1, " %lf/%lf/%lf/%lf",
                       &min_ms, &avg_ms, &max_ms, &mdev_ms) == 4)
                got = 1;
    }
    pclose(fp);
    if (!got) { fprintf(stderr, "bping: could not reach %s\n", dest); return 1; }

    if (!have_tgt_ecef && !tgt_geo.valid) {
        char target_ip[64] = "";
        bs_resolve_to_ip(dest, target_ip, sizeof(target_ip));
        if (target_ip[0])
            tgt_geo = bs_geolocate(target_ip);
    }

    if (!have_my_ecef && !my_geo.valid)
        my_geo = bs_geolocate(NULL);

    if (have_my_ecef && !my_geo.valid)
        bs_ecef_to_geo(my_ecef, &my_geo, "[ecef]");
    if (have_tgt_ecef && !tgt_geo.valid)
        bs_ecef_to_geo(tgt_ecef, &tgt_geo, "[ecef]");

    int    have_ecef = have_my_ecef && have_tgt_ecef;
    double ecef_d    = have_ecef ? bs_ecef_chord_mbm(my_ecef, tgt_ecef) : 0.0;

    print_stats(dest, min_ms, avg_ms, max_ms, mdev_ms,
                have_ecef, ecef_d, &my_geo, &tgt_geo);

    if (show_hops)
        print_hops(dest);

    return 0;
}
