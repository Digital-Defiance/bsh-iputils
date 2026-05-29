/*
 * baudit.c — BrightSpace multi-anchor distance bounding
 *
 * Measures the RTT from this machine to a target, then computes the
 * "constraint ring" (radius = RTT/2 at speed of light) around each
 * measurement anchor.  With multiple anchors from different vantage points,
 * it bounds where the target can physically be.
 *
 * Self anchor: probed live via ping.  Additional anchors supplied via
 * --anchor=lat,lon,rtt_ms (can repeat).  Useful for pasting in RTT values
 * measured from other known locations (cloud VMs, friends, etc.)
 *
 * Output:
 *   - Per-anchor ring: center, radius_km, whether geoIP is inside
 *   - Centroid estimate: average of anchor centers weighted by 1/radius
 *   - Consistency score: fraction of anchors whose ring contains geoIP
 *   - If all rings agree: "consistent — target likely near <city>"
 *   - If rings disagree: "inconsistent — possible anycast / VPN / bad geoIP"
 *
 * Location provider:
 *   BrightNexus bridge — BrightLink LINK_GEO_GET (RFC §9.4) via the
 *   'bsh-geo --get --format both --json' helper.  The bridge gates the
 *   request through the user's geo:precise ACL grant.
 */

#include "../brightspace.h"
#include "../brightlink_glue.h"
#include "../iputils_color.h"

#define MAX_ANCHORS 32

typedef struct {
    double lat, lon;
    double rtt_ms;
    char   label[64];   /* optional: city name or description */
    int    from_cli;    /* 1 = from --anchor=, 0 = self */
} anchor_t;

static void usage(void)
{
    fprintf(stderr,
        "\nUsage\n"
        "  baudit [options] <destination>\n"
        "\nOptions:\n"
        "  --anchor=lat,lon,rtt_ms[,label]  Add an external measurement anchor.\n"
        "                                   Repeat for multiple anchors.\n"
        "                                   rtt_ms is the round-trip time in ms\n"
        "                                   from that location to the target.\n"
        "  --my-coord=lat,lon               My position (goes in shell history)\n"
        "  --my-ecef=x,y,z                  My ECEF position (BrightMeters)\n"
        "  -c <count>                       Ping count for self-probe (default: 5)\n"
        "  --reset-brightlink-pin           Forget the BrightNexus TOFU pin and exit\n"
        IPU_COLOR_USAGE
        "\nLocation provider (BrightNexus / BrightLink):\n"
        "  baudit shells out to 'bsh-geo --get --format both --json' to read\n"
        "  the current location from the BrightNexus bridge. The bridge gates the\n"
        "  request through the user's geo:precise ACL grant. No env vars are read.\n"
        "\nExample:\n"
        "  # Probe from here + paste in two VPS measurements:\n"
        "  baudit --anchor=51.5074,-0.1278,42.3,London \\\n"
        "         --anchor=35.6762,139.6503,118.7,Tokyo \\\n"
        "         8.8.8.8\n");
    exit(2);
}

/* Parse "--anchor=lat,lon,rtt_ms[,label]" */
static int parse_anchor(const char *s, anchor_t *a)
{
    double lat, lon, rtt;
    int n = 0;
    if (sscanf(s, "%lf,%lf,%lf%n", &lat, &lon, &rtt, &n) < 3) return 0;
    a->lat    = lat;
    a->lon    = lon;
    a->rtt_ms = rtt;
    a->from_cli = 1;
    /* optional label after third comma */
    const char *rest = s + n;
    if (*rest == ',') {
        rest++;
        int l = (int)strlen(rest);
        if (l >= (int)sizeof(a->label)) l = (int)sizeof(a->label) - 1;
        strncpy(a->label, rest, (size_t)l);
        a->label[l] = '\0';
    }
    return 1;
}

/* Compute the centroid weighted by 1/radius_km (tighter constraints count more) */
static void weighted_centroid(const anchor_t *anchors, const double *radii,
                               int n, double *out_lat, double *out_lon)
{
    double wlat = 0, wlon = 0, wtot = 0;
    for (int i = 0; i < n; ++i) {
        if (radii[i] <= 0.0) continue;
        double w = 1.0 / radii[i];
        wlat += anchors[i].lat * w;
        wlon += anchors[i].lon * w;
        wtot += w;
    }
    if (wtot > 0.0) {
        *out_lat = wlat / wtot;
        *out_lon = wlon / wtot;
    } else {
        *out_lat = 0.0;
        *out_lon = 0.0;
    }
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

    anchor_t anchors[MAX_ANCHORS];
    int nanchors = 0;
    int ping_count = 5;
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
        } else if (strncmp(argv[i], "--anchor=", 9) == 0) {
            if (nanchors < MAX_ANCHORS) {
                anchor_t a;
                memset(&a, 0, sizeof(a));
                if (parse_anchor(argv[i] + 9, &a))
                    anchors[nanchors++] = a;
                else
                    fprintf(stderr, "baudit: bad --anchor format: %s\n", argv[i] + 9);
            }
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            ping_count = atoi(argv[++i]);
            if (ping_count < 1 || ping_count > 100) ping_count = 5;
        } else if (strncmp(argv[i], "-c", 2) == 0) {
            ping_count = atoi(argv[i] + 2);
            if (ping_count < 1 || ping_count > 100) ping_count = 5;
        } else if (strcmp(argv[i], "-h") == 0
                || strcmp(argv[i], "--help") == 0) {
            usage();
        } else if (argv[i][0] != '-') {
            dest = argv[i];
        }
    }
    if (!dest) usage();

    bl_glue_get_geo(argv[0], &my_ecef, &have_my_ecef, &my_geo);
    if (!have_my_ecef && !my_geo.valid)
        my_geo = bs_geolocate(NULL);
    if (have_my_ecef && !my_geo.valid)
        bs_ecef_to_geo(my_ecef, &my_geo, "[ecef]");

    /* If brightlink missed, annotate the geoIP tag with the reason. */
    bl_glue_annotate_tag(&my_geo);

    /* Resolve and geolocate target */
    char dest_ip[64] = "";
    bs_resolve_to_ip(dest, dest_ip, sizeof(dest_ip));
    bs_geo_t tgt_geo;
    memset(&tgt_geo, 0, sizeof(tgt_geo));
    if (dest_ip[0])
        tgt_geo = bs_geolocate(dest_ip);

    /* Probe self → target */
    double self_rtt_ms = -1.0;
    if (my_geo.valid) {
        fprintf(stderr, "Probing %s (%d pings) ...\n", dest, ping_count);
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ping -c %d %s 2>&1", ping_count, dest);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char buf[256];
            while (fgets(buf, sizeof(buf), fp)) {
                const char *eq;
                if ((eq = strstr(buf, "=")) && strstr(buf, "min/avg/max")) {
                    double vmin, vavg;
                    if (sscanf(eq + 1, " %lf/%lf", &vmin, &vavg) >= 2)
                        self_rtt_ms = vavg;
                }
            }
            pclose(fp);
        }
    }

    /* Build self anchor */
    anchor_t self_anchor;
    memset(&self_anchor, 0, sizeof(self_anchor));
    if (my_geo.valid && self_rtt_ms > 0.0) {
        self_anchor.lat    = my_geo.lat;
        self_anchor.lon    = my_geo.lon;
        self_anchor.rtt_ms = self_rtt_ms;
        self_anchor.from_cli = 0;
        char lbl[80] = "";
        bs_geo_label(&my_geo, lbl, sizeof(lbl));
        snprintf(self_anchor.label, sizeof(self_anchor.label),
                 "%s %s", lbl[0] ? lbl : "self", my_geo.tag);

        /* Prepend self anchor before CLI anchors */
        if (nanchors < MAX_ANCHORS) {
            memmove(&anchors[1], &anchors[0],
                    (size_t)nanchors * sizeof(anchor_t));
            anchors[0] = self_anchor;
            nanchors++;
        }
    }

    /* ── Print target info ───────────────────────────────── */
    printf("%sbaudit — %s%s", IPU(c, title), dest, IPU(c, reset));
    if (dest_ip[0] && strcmp(dest_ip, dest) != 0)
        printf(" (%s)", dest_ip);
    if (tgt_geo.valid) {
        char lbl[80] = "";
        bs_geo_label(&tgt_geo, lbl, sizeof(lbl));
        if (lbl[0]) printf("  %s%s%s", IPU(c, location), lbl, IPU(c, reset));
        printf("  %s%s%s", IPU(c, tag), tgt_geo.tag, IPU(c, reset));
    }
    printf("\n");

    if (nanchors == 0) {
        fprintf(stderr, "baudit: no anchors — could not probe or geolocate self\n");
        return 1;
    }

    /* ── Per-anchor analysis ─────────────────────────────── */
    printf("\n");
    printf("  %s%-28s%s  %s%-9s%s  %s%-9s%s  %s%-9s%s  %s%-10s%s  %s%-8s%s  %s%-8s%s  %s%-8s%s\n",
           IPU(c, header), "anchor", IPU(c, reset),
           IPU(c, header), "x(BM)", IPU(c, reset),
           IPU(c, header), "y(BM)", IPU(c, reset),
           IPU(c, header), "z(BM)", IPU(c, reset),
           IPU(c, header), "rtt(ms)", IPU(c, reset),
           IPU(c, header), "ring(km)", IPU(c, reset),
           IPU(c, header), "tgt_dist", IPU(c, reset),
           IPU(c, header), "in_ring?", IPU(c, reset));
    printf("  %-28s  %9s  %9s  %9s  %10s  %8s  %8s  %8s\n",
           "----------------------------",
           "---------", "---------", "---------",
           "----------",
           "--------", "--------", "--------");

    double radii[MAX_ANCHORS];
    int    inside[MAX_ANCHORS];
    int    n_inside = 0;

    for (int i = 0; i < nanchors; ++i) {
        const anchor_t *a = &anchors[i];
        /* Ring radius = RTT/2 converted to km (speed of light in fiber ~c/1.5,
         * but we use c as the absolute upper bound — ring gives min separation) */
        double radius_km = (a->rtt_ms / 2.0) * BS_KM_PER_MBM;
        radii[i] = radius_km;

        double tgt_dist_km = -1.0;
        int    in_ring     = 0;
        if (tgt_geo.valid) {
            tgt_dist_km = bs_haversine_km(a->lat, a->lon,
                                          tgt_geo.lat, tgt_geo.lon);
            in_ring = (tgt_dist_km <= radius_km);
            if (in_ring) n_inside++;
        }
        inside[i] = in_ring;

        char dist_buf[16] = "?", in_buf[8] = "?";
        const char *in_style;
        if (tgt_dist_km >= 0.0)
            snprintf(dist_buf, sizeof(dist_buf), "%.0f", tgt_dist_km);
        if (tgt_geo.valid)
            snprintf(in_buf, sizeof(in_buf), "%s", in_ring ? "yes" : "NO");
        in_style = ipu_in_ring_style(c, in_ring);

        double ax, ay, az;
        bs_latlon_to_ecef(a->lat, a->lon, &ax, &ay, &az);
        printf("  %s%-28s%s  %s%9.5f%s  %s%9.5f%s  %s%9.5f%s  %s%10.3f%s  %s%8.0f%s  %s%8s%s  %s%8s%s\n",
               IPU(c, host), a->label[0] ? a->label : "(unnamed)", IPU(c, reset),
               IPU(c, value), ax, IPU(c, reset),
               IPU(c, value), ay, IPU(c, reset),
               IPU(c, value), az, IPU(c, reset),
               IPU(c, rtt), a->rtt_ms, IPU(c, reset),
               IPU(c, value), radius_km, IPU(c, reset),
               IPU(c, value), dist_buf, IPU(c, reset),
               in_style, in_buf, IPU(c, reset));
    }

    /* ── Centroid estimate ───────────────────────────────── */
    printf("\n");
    double est_lat = 0, est_lon = 0;
    weighted_centroid(anchors, radii, nanchors, &est_lat, &est_lon);
    double cx, cy, cz;
    bs_latlon_to_ecef(est_lat, est_lon, &cx, &cy, &cz);
    printf("  ");
    ipu_fprint_label(stdout, c, c->label, "weighted centroid", 22);
    printf("     = %s%.5f, %.5f, %.5f%s %sBM%s\n",
           IPU(c, value), cx, cy, cz, IPU(c, reset),
           IPU(c, unit), IPU(c, reset));

    double tightest_km = 1e9;
    for (int i = 0; i < nanchors; ++i)
        if (radii[i] < tightest_km) tightest_km = radii[i];
    printf("  ");
    ipu_fprint_label(stdout, c, c->label, "tightest ring radius", 22);
    printf("  = %s%.0f%s %skm%s  (%starget within %.0f km of best anchor%s)\n",
           IPU(c, value), tightest_km, IPU(c, reset),
           IPU(c, unit), IPU(c, reset),
           IPU(c, detail), tightest_km, IPU(c, reset));

    if (tgt_geo.valid && nanchors > 0) {
        double score = (double)n_inside / nanchors * 100.0;
        const char *score_style = (n_inside == nanchors) ? c->ok : c->warn;
        printf("  ");
        ipu_fprint_label(stdout, c, c->label, "geoIP consistency", 22);
        printf("     = %s%d/%d%s anchors (%s%.0f%%%s)\n",
               IPU(c, value), n_inside, nanchors, IPU(c, reset),
               score_style, score, IPU(c, reset));
        if (n_inside == nanchors) {
            char lbl[80] = "";
            bs_geo_label(&tgt_geo, lbl, sizeof(lbl));
            printf("  ");
            ipu_fprint_label(stdout, c, c->label, "verdict", 22);
            printf("               : %sconsistent — geoIP %s is plausible%s\n",
                   IPU(c, ok),
                   lbl[0] ? lbl : tgt_geo.tag,
                   IPU(c, reset));
        } else {
            printf("  ");
            ipu_fprint_label(stdout, c, c->label, "verdict", 22);
            printf("               : %sINCONSISTENT — geoIP placement conflicts\n"
                   "                          with %d anchor(s) — possible anycast / VPN%s\n",
                   IPU(c, bad), nanchors - n_inside, IPU(c, reset));
        }
    }

    if (tgt_geo.valid) {
        double cdist = bs_haversine_km(est_lat, est_lon,
                                       tgt_geo.lat, tgt_geo.lon);
        printf("  ");
        ipu_fprint_label(stdout, c, c->label, "centroid → geoIP", 22);
        printf("      = %s%.0f%s %skm%s\n",
               IPU(c, value), cdist, IPU(c, reset),
               IPU(c, unit), IPU(c, reset));
    }

    return 0;
}
