/*
 * brightspace.h — Shared BrightSpace types, math, and geo helpers
 *
 * Include this header in all b* tools.  All functions are static inline
 * so each translation unit gets its own copy with no linker conflicts.
 *
 * BSPACE Coordinate Protocol:
 *   --my-ecef=x,y,z       ECEF in BrightMeters (CLI, audit-grade)  [ecef]
 *   --my-coord=lat,lon     Decimal degrees      (CLI, explicit)     [coord]
 *   $BSPACE_ECEF=x,y,z     ECEF via env        (no history leak)   [env:ecef]
 *   $BSPACE_COORD=lat,lon   Lat/lon pair via env (no history leak) [env]
 *   $BSPACE_LAT + $BSPACE_LON  Separate lat/lon  (no history leak) [env:ll]
 *   $LAT + $LON             Plain alternate lat/lon (no history)   [ll]
 *   auto ip-api.com lookup of public IP        (fallback)           [~geoIP]
 *
 * Units:
 *   1 BrightMeter (BM)       = distance light travels in 1 s  = 299,792 km
 *   1 milliBrightMeter (mBM) = light in 1 ms                  = 299.792 km
 *   1 milliday (md)          = 86,400 ms
 *   light-floor              = RTT/2 in mBM  (absolute lower bound on dist)
 *   efficiency               = (geo_dist_mBM / light-floor) × 100%
 */

#ifndef BRIGHTSPACE_H
#define BRIGHTSPACE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BS_MS_PER_MILLIDAY   86400.0
#define BS_KM_PER_MBM        299.792     /* 1 mBM = 299.792 km */
#define BS_EARTH_RADIUS_KM   6371.0

/* ── Coordinate types ─────────────────────────────────────────────────── */

typedef struct { double x, y, z; } bs_ecef_t;  /* BrightMeters */

typedef struct {
    double lat, lon;    /* decimal degrees */
    int    valid;
    char   city[64];
    char   country[64];
    char   tag[16];     /* "[ecef]" "[coord]" "[env]" "[~geoIP]" */
} bs_geo_t;

/* ── Unit conversion ──────────────────────────────────────────────────── */

static inline double bs_to_md(double ms)       { return ms / BS_MS_PER_MILLIDAY; }
static inline double bs_km_to_mbm(double km)   { return km / BS_KM_PER_MBM; }
static inline double bs_mbm_to_km(double mbm)  { return mbm * BS_KM_PER_MBM; }

/* ── Coordinate parsing ───────────────────────────────────────────────── */

static inline int bs_parse_ecef(const char *s, bs_ecef_t *e)
{
    return sscanf(s, "%lf,%lf,%lf", &e->x, &e->y, &e->z) == 3;
}

static inline int bs_parse_latlon(const char *s, double *lat, double *lon)
{
    return sscanf(s, "%lf,%lf", lat, lon) == 2;
}

/* Convert ECEF (BrightMeters) to lat/lon and populate a geo_t */
static inline void bs_ecef_to_geo(bs_ecef_t e, bs_geo_t *g, const char *tag)
{
    double bm = sqrt(e.x*e.x + e.y*e.y + e.z*e.z);
    if (bm <= 0.0) return;
    g->lat   = asin(e.z / bm) * 180.0 / M_PI;
    g->lon   = atan2(e.y, e.x) * 180.0 / M_PI;
    g->valid = 1;
    snprintf(g->tag, sizeof(g->tag), "%s", tag ? tag : "[ecef]");
}

/* Convert lat/lon (decimal degrees) to ECEF BrightMeters */
static inline void bs_latlon_to_ecef(double lat, double lon,
                                     double *x, double *y, double *z)
{
    /* 1 BM = BS_KM_PER_MBM * 1000 km = 299792 km */
    const double R_BM = BS_EARTH_RADIUS_KM / (BS_KM_PER_MBM * 1000.0);
    double lat_r = lat * M_PI / 180.0;
    double lon_r = lon * M_PI / 180.0;
    *x = R_BM * cos(lat_r) * cos(lon_r);
    *y = R_BM * cos(lat_r) * sin(lon_r);
    *z = R_BM * sin(lat_r);
}

/* Format a geo_t position as "x, y, z BM" (ECEF in BrightMeters) */
static inline void bs_geo_ecef_str(const bs_geo_t *g, char *buf, size_t n)
{
    if (!g->valid) { snprintf(buf, n, "?"); return; }
    double x, y, z;
    bs_latlon_to_ecef(g->lat, g->lon, &x, &y, &z);
    snprintf(buf, n, "%.5f, %.5f, %.5f BM", x, y, z);
}

/* ── Distance formulas ────────────────────────────────────────────────── */

/* Euclidean chord distance in mBM (ECEF coords in BrightMeters) */
static inline double bs_ecef_chord_mbm(bs_ecef_t a, bs_ecef_t b)
{
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrt(dx*dx + dy*dy + dz*dz) * 1000.0;   /* BM → mBM */
}

/* Haversine great-circle distance in km */
static inline double bs_haversine_km(double la1, double lo1,
                                     double la2, double lo2)
{
    const double R   = BS_EARTH_RADIUS_KM;
    const double toR = M_PI / 180.0;
    double dlat = (la2 - la1) * toR;
    double dlon = (lo2 - lo1) * toR;
    double a = sin(dlat/2.0)*sin(dlat/2.0)
             + cos(la1*toR)*cos(la2*toR)*sin(dlon/2.0)*sin(dlon/2.0);
    return R * 2.0 * atan2(sqrt(a), sqrt(1.0-a));
}

/* ── Network helpers ──────────────────────────────────────────────────── */

/* Resolve hostname → dotted-quad IPv4.  Returns 1 on success. */
static inline int bs_resolve_to_ip(const char *host, char *out, size_t outsz)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;
    inet_ntop(AF_INET,
              &((struct sockaddr_in *)res->ai_addr)->sin_addr,
              out, (socklen_t)outsz);
    freeaddrinfo(res);
    return 1;
}

/* Geolocate an IP via ip-api.com.  ip=NULL or "" → caller's public IP. */
static inline bs_geo_t bs_geolocate(const char *ip)
{
    bs_geo_t g;
    memset(&g, 0, sizeof(g));
    snprintf(g.tag, sizeof(g.tag), "[~geoIP]");

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "curl -s --max-time 3 "
        "'http://ip-api.com/json/%s?fields=status,city,country,lat,lon'",
        (ip && *ip) ? ip : "");
    FILE *fp = popen(cmd, "r");
    if (!fp) return g;
    char buf[512];
    memset(buf, 0, sizeof(buf));
    fgets(buf, sizeof(buf) - 1, fp);
    pclose(fp);
    if (!strstr(buf, "\"success\"")) return g;

    char *p;
    if ((p = strstr(buf, "\"lat\":")))       g.lat = strtod(p + 6, NULL);
    if ((p = strstr(buf, "\"lon\":")))       g.lon = strtod(p + 6, NULL);
    if ((p = strstr(buf, "\"city\":\""))) {
        p += 8;
        char *e = strchr(p, '"');
        if (e) {
            int l = (int)(e - p);
            if (l >= (int)sizeof(g.city)) l = (int)sizeof(g.city) - 1;
            strncpy(g.city, p, (size_t)l);
            g.city[l] = '\0';
        }
    }
    if ((p = strstr(buf, "\"country\":\""))) {
        p += 11;
        char *e = strchr(p, '"');
        if (e) {
            int l = (int)(e - p);
            if (l >= (int)sizeof(g.country)) l = (int)sizeof(g.country) - 1;
            strncpy(g.country, p, (size_t)l);
            g.country[l] = '\0';
        }
    }
    g.valid = 1;
    return g;
}

/* ── BSPACE env var loading ───────────────────────────────────────────── */

/*
 * Load my coordinates from env vars into *e / *g.
 * Call AFTER CLI flag parsing.  Only sets fields not already set.
 *
 * Priority (highest → lowest):
 *   1. $BSPACE_ECEF=x,y,z          BrightSpace ECEF in BrightMeters
 *   2. $BSPACE_COORD=lat,lon        BrightSpace lat/lon pair
 *   3. $BSPACE_LAT + $BSPACE_LON    BrightSpace separate lat/lon
 *   4. $LAT + $LON                  Traditional separate lat/lon
 */
static inline void bs_load_env(bs_ecef_t *e, int *have_ecef, bs_geo_t *g)
{
    /* 1. BSPACE_ECEF */
    if (!*have_ecef && !g->valid) {
        const char *ev = getenv("BSPACE_ECEF");
        if (ev && bs_parse_ecef(ev, e))
            *have_ecef = 1;
    }
    /* 2. BSPACE_COORD */
    if (!*have_ecef && !g->valid) {
        const char *ev = getenv("BSPACE_COORD");
        if (ev) {
            double lat, lon;
            if (bs_parse_latlon(ev, &lat, &lon)) {
                g->lat = lat; g->lon = lon; g->valid = 1;
                snprintf(g->tag, sizeof(g->tag), "[env]");
            }
        }
    }
    /* 3. BSPACE_LAT + BSPACE_LON */
    if (!*have_ecef && !g->valid) {
        const char *elat = getenv("BSPACE_LAT");
        const char *elon = getenv("BSPACE_LON");
        if (elat && elon) {
            char *end;
            double lat = strtod(elat, &end);
            if (end != elat) {
                double lon = strtod(elon, &end);
                if (end != elon) {
                    g->lat = lat; g->lon = lon; g->valid = 1;
                    snprintf(g->tag, sizeof(g->tag), "[env:ll]");
                }
            }
        }
    }
    /* 4. LAT + LON (traditional) */
    if (!*have_ecef && !g->valid) {
        const char *elat = getenv("LAT");
        const char *elon = getenv("LON");
        if (elat && elon) {
            char *end;
            double lat = strtod(elat, &end);
            if (end != elat) {
                double lon = strtod(elon, &end);
                if (end != elon) {
                    g->lat = lat; g->lon = lon; g->valid = 1;
                    snprintf(g->tag, sizeof(g->tag), "[ll]");
                }
            }
        }
    }
}

/* ── Traceroute line parsing helpers ──────────────────────────────────── */

/* Return average of all "N.NNN ms" tokens in a traceroute line, or -1 */
static inline double bs_line_avg_rtt(const char *line)
{
    double sum = 0.0;
    int count  = 0;
    const char *p = line;
    while (*p) {
        double val; char unit[8]; int n = 0;
        if (sscanf(p, " %lf %7s%n", &val, unit, &n) >= 2
                && strcmp(unit, "ms") == 0) {
            sum += val; count++; p += n;
        } else { p++; }
    }
    return count > 0 ? sum / count : -1.0;
}

/*
 * Extract individual RTT samples from a traceroute line.
 * Writes up to max_samples values into out[].  Returns count written.
 */
static inline int bs_line_rtts(const char *line, double *out, int max_samples)
{
    int count  = 0;
    const char *p = line;
    while (*p && count < max_samples) {
        double val; char unit[8]; int n = 0;
        if (sscanf(p, " %lf %7s%n", &val, unit, &n) >= 2
                && strcmp(unit, "ms") == 0) {
            out[count++] = val; p += n;
        } else { p++; }
    }
    return count;
}

/* Extract "hostname (ip)" or "* * *" from a traceroute hop line into out[] */
static inline void bs_hop_host(const char *line, char *out, size_t outsz)
{
    const char *p = line;
    while (*p == ' ') p++;
    while (*p && *p != ' ') p++;   /* skip hop number */
    while (*p == ' ') p++;

    if (*p == '*') { snprintf(out, outsz, "* * *"); return; }

    const char *start = p;
    const char *end   = p + strlen(p);
    for (; *p; p++) {
        double dummy; char u[8]; int n;
        if (*p == ' ' && sscanf(p, " %lf %7s%n", &dummy, u, &n) >= 2
                      && strcmp(u, "ms") == 0) {
            end = p; break;
        }
    }
    int len = (int)(end - start);
    if (len <= 0) { snprintf(out, outsz, "?"); return; }
    if ((size_t)len >= outsz) len = (int)outsz - 1;
    strncpy(out, start, (size_t)len); out[len] = '\0';
    while (len > 0 && out[len - 1] == ' ') out[--len] = '\0';
}

/*
 * Extract the IP address from a "hostname (ip)" or bare-IP hop token.
 * Writes into ip_out (size ip_outsz).
 */
static inline void bs_hop_ip(const char *host, char *ip_out, size_t ip_outsz)
{
    if (strcmp(host, "* * *") == 0 || strcmp(host, "?") == 0) {
        ip_out[0] = '\0'; return;
    }
    const char *lp = strchr(host, '(');
    if (lp) {
        const char *rp = strchr(lp + 1, ')');
        if (rp) {
            int il = (int)(rp - lp - 1);
            if (il > 0 && il < (int)ip_outsz) {
                strncpy(ip_out, lp + 1, (size_t)il);
                ip_out[il] = '\0';
                return;
            }
        }
    }
    snprintf(ip_out, ip_outsz, "%s", host);
}

/* Build a "city, Country" string from a geo_t; writes "" if nothing known */
static inline void bs_geo_label(const bs_geo_t *g, char *out, size_t outsz)
{
    if (!g || !g->valid) { out[0] = '\0'; return; }
    if (g->city[0] && g->country[0])
        snprintf(out, outsz, "%s, %s", g->city, g->country);
    else if (g->city[0])
        snprintf(out, outsz, "%s", g->city);
    else if (g->country[0])
        snprintf(out, outsz, "%s", g->country);
    else
        out[0] = '\0';
}

#endif /* BRIGHTSPACE_H */
