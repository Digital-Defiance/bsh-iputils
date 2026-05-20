/*
 * brightspace.h — Shared BrightSpace types, math, and geo helpers
 *
 * Include this header in all b* tools.  All functions are static inline
 * so each translation unit gets its own copy with no linker conflicts.
 *
 * BSPACE Coordinate Protocol:
 *   --my-ecef=x,y,z       ECEF in BrightMeters (CLI, audit-grade)  [ecef]
 *   --my-coord=lat,lon     Decimal degrees      (CLI, explicit)     [coord]
 *   $BSH_GEO_SOCK          BSH SDI v2 geo socket (RFC SDI v2 §8)   [sdi] / [sdi:ecef]
 *   auto ip-api.com lookup of public IP        (fallback)           [~geoIP]
 *
 * Location is no longer read from plain environment variables.  The SDI v2
 * geo socket (§8 of RFC SDI v2) is the secure replacement: the agent holds
 * coordinates; child processes query it over a UID-gated Unix-domain socket.
 * Each b* tool must be listed in ~/.config/bsh/geo-allow for the agent to
 * serve it.  Use 'bsh-geo --exec -- <tool>' as an escape hatch when needed.
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
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

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
    char   tag[16];     /* "[ecef]" "[coord]" "[sdi]" "[sdi:ecef]" "[~geoIP]" */
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

/* ── SDI v2 geo-context client (RFC SDI v2 §8) ───────────────────────── */

/*
 * Helper: read the geo socket path from the path-file written by the agent.
 * $BSH_GEO_SOCK contains the path-file path (not the socket path directly).
 * Returns 1 on success, 0 on failure.
 */
static inline int bs_sdi_read_sock_path(const char *path_file,
                                         char *sock_path, size_t sz)
{
    FILE *fp = fopen(path_file, "r");
    if (!fp) return 0;
    char *ok = fgets(sock_path, (int)sz, fp);
    fclose(fp);
    if (!ok) return 0;
    size_t n = strlen(sock_path);
    while (n > 0 && (sock_path[n-1] == '\n' || sock_path[n-1] == '\r'))
        sock_path[--n] = '\0';
    return n > 0;
}

/*
 * Query the BSH SDI v2 geo socket for the current location fix.
 * Call AFTER CLI flag parsing; no-ops if coords are already set.
 *
 * Wire protocol (RFC SDI v2 §8.3):
 *   1. Read $BSH_GEO_SOCK → path file → socket path
 *   2. Connect (UID-authenticated by the agent; tool must be in geo-allow)
 *   3. Send:    {"op":"get","require_altitude":false}\n
 *   4. Receive: single JSON line — §6.2 success payload or §6.3 error
 *   5. Close
 *
 * On success populates *g with tag [sdi].  If the response includes a
 * spacetime block (BrightMeters), also populates *e with tag [sdi:ecef].
 * Silently returns on any failure; caller falls through to auto-geoIP.
 */
static inline void bs_sdi_get_geo(bs_ecef_t *e, int *have_ecef, bs_geo_t *g)
{
    /* Skip if coords already provided via CLI flags */
    if (*have_ecef || g->valid) return;

    const char *path_file = getenv("BSH_GEO_SOCK");
    if (!path_file || !*path_file) return;

    char sock_path[512];
    int  fd = -1;

    /* Try up to 2 times; re-read path file on retry (agent may have restarted)
     * per RFC SDI v2 §8.2. */
    for (int attempt = 0; attempt < 2 && fd < 0; ++attempt) {
        if (!bs_sdi_read_sock_path(path_file, sock_path, sizeof(sock_path)))
            return;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (strlen(sock_path) >= sizeof(addr.sun_path)) return;
        strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s < 0) return;

        /* 3-second send/receive timeout so the tool never hangs */
        struct timeval tv;
        tv.tv_sec = 3; tv.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            fd = s;
        else
            close(s);
    }
    if (fd < 0) return;

    /* Send request */
    static const char req[] = "{\"op\":\"get\",\"require_altitude\":false}\n";
    if (write(fd, req, sizeof(req) - 1) != (ssize_t)(sizeof(req) - 1)) {
        close(fd); return;
    }

    /* Read single JSON line response (up to 4 KiB) */
    char resp[4096];
    int pos = 0;
    while (pos < (int)sizeof(resp) - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) break;
        if (c == '\n') break;
        resp[pos++] = c;
    }
    resp[pos] = '\0';
    close(fd);

    if (!pos || strstr(resp, "\"error\"")) return;

    /* Parse geodetic.latitude / geodetic.longitude (§6.2) */
    const char *p;
    double lat = 0.0, lon = 0.0;
    int have_lat = 0, have_lon = 0;
    if ((p = strstr(resp, "\"latitude\":")))
        { p += 11; char *ep; lat = strtod(p, &ep); if (ep != p) have_lat = 1; }
    if ((p = strstr(resp, "\"longitude\":")))
        { p += 12; char *ep; lon = strtod(p, &ep); if (ep != p) have_lon = 1; }
    if (!have_lat || !have_lon) return;

    g->lat = lat; g->lon = lon; g->valid = 1;
    snprintf(g->tag, sizeof(g->tag), "[sdi]");

    /* Parse spacetime.{x,y,z} (BrightMeters) → bs_ecef_t if present.
     * Extract the spacetime block first to avoid matching ecef.{x,y,z}. */
    const char *st = strstr(resp, "\"spacetime\":");
    if (st) {
        const char *brace = strchr(st + 12, '{');
        if (brace) {
            const char *end = strchr(brace + 1, '}');
            if (end) {
                char block[256];
                size_t blen = (size_t)(end - brace + 1);
                if (blen < sizeof(block)) {
                    memcpy(block, brace, blen);
                    block[blen] = '\0';
                    double sx = 0.0, sy = 0.0, sz = 0.0;
                    int hx = 0, hy = 0, hz = 0;
                    const char *q;
                    if ((q = strstr(block, "\"x\":"))) { q += 4; char *ep; sx = strtod(q, &ep); if (ep != q) hx = 1; }
                    if ((q = strstr(block, "\"y\":"))) { q += 4; char *ep; sy = strtod(q, &ep); if (ep != q) hy = 1; }
                    if ((q = strstr(block, "\"z\":"))) { q += 4; char *ep; sz = strtod(q, &ep); if (ep != q) hz = 1; }
                    if (hx && hy && hz) {
                        e->x = sx; e->y = sy; e->z = sz;
                        *have_ecef = 1;
                        snprintf(g->tag, sizeof(g->tag), "[sdi:ecef]");
                    }
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
