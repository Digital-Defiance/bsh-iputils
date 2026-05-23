/*
 * brightlink_glue.h — bright-iputils-side adapter on top of libBrightLink.
 *
 * The public libBrightLink API is handle-based: callers allocate a
 * bl_client_t, configure a pin store, run bl_geo_get, and free both at
 * exit. That's the right shape for general consumers (bsh holds a single
 * shell-wide client; daemons might hold one per user).
 *
 * For one-shot CLI tools like bping/btraceroute/bmtr/baudit, the
 * boilerplate is identical four times over: derive a binary id from
 * argv[0], create a file-backed pin store under
 * ~/.brightchain/iputils-pins/, hold one client for the lifetime of
 * main(), populate a (bs_ecef_t, bs_geo_t) pair from the response,
 * release both at exit.
 *
 * This header does that boilerplate exactly once. Tools call
 * bl_glue_get_geo(argv[0], &my_ecef, &have_my_ecef, &my_geo) and that's
 * the entire integration. On any failure the call falls through silently
 * (geoIP fallback path takes over per BSPACE.md). Diagnostics are routed
 * to stderr only when BRIGHTLINK_DEBUG=1 is set.
 */

#ifndef BRIGHTLINK_GLUE_H
#define BRIGHTLINK_GLUE_H

#include "brightspace.h"
#include "brightlink/brightlink.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Resolve $HOME or fall back to getpwuid. Returns NULL if neither works. */
static inline const char *bl_glue_home(void)
{
    const char *h = getenv("HOME");
    if (h != NULL && h[0] != '\0') return h;
    struct passwd *pw = getpwuid(getuid());
    return (pw != NULL) ? pw->pw_dir : NULL;
}

/* Sanitise argv[0] to a binary_id acceptable to bl_pin_store_file. The
 * pin store rejects anything that isn't [A-Za-z0-9._-], so we strip path
 * components first (basename) and then walk the result, falling back to
 * "iputils" if nothing usable remains. */
static inline void bl_glue_binary_id(const char *argv0, char *out, size_t cap)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", (argv0 && argv0[0]) ? argv0 : "iputils");
    const char *base = basename(tmp);

    size_t j = 0;
    for (size_t i = 0; base[i] != '\0' && j + 1 < cap; i++) {
        char c = base[i];
        int ok = (c >= 'A' && c <= 'Z') ||
                 (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') ||
                 c == '.' || c == '-' || c == '_';
        if (ok) out[j++] = c;
    }
    out[j] = '\0';
    if (j == 0 || out[0] == '.') {
        snprintf(out, cap, "iputils");
    }
}

/* Per-binary scratch buffer that records *why* the last brightlink
 * attempt failed, so the geoIP fallback path can annotate its output
 * tag (and so callers can surface it elsewhere if they want). Reasons
 * are short slugs ("pin-mismatch", "refused", "crypto", "proto", ...)
 * picked to fit inside bs_geo_t.tag once wrapped in "[~geoIP/<r>]".
 * Empty string means "no relevant failure recorded". Not thread-safe;
 * the b* tools are single-threaded. */
static inline char *bl_glue_reason_buf(void)
{
    static char buf[16] = "";
    return buf;
}

static inline const char *bl_glue_last_reason(void)
{
    const char *r = bl_glue_reason_buf();
    return (r[0] != '\0') ? r : NULL;
}

/* If the geoIP fallback wrote "[~geoIP]" into g->tag and we have a
 * brightlink failure reason recorded, rewrite the tag to
 * "[~geoIP/<reason>]" so users see *why* we fell back without having
 * to consult stderr. No-op when no reason is recorded or when the tag
 * was set by some other path (e.g. "[ecef]", "[coord]"). */
static inline void bl_glue_annotate_tag(bs_geo_t *g)
{
    const char *r = bl_glue_last_reason();
    if (r == NULL) return;
    if (strncmp(g->tag, "[~geoIP", 7) != 0) return;
    /* Already annotated (contains a '/') — leave it alone. */
    if (strchr(g->tag, '/') != NULL) return;
    char new_tag[sizeof(g->tag)];
    snprintf(new_tag, sizeof(new_tag), "[~geoIP/%s]", r);
    snprintf(g->tag, sizeof(g->tag), "%s", new_tag);
}

/* Compute the path to this binary's TOFU pin file. */
static inline int bl_glue_pin_path(const char *argv0, char *out, size_t cap)
{
    const char *home = bl_glue_home();
    if (home == NULL) return -1;
    char binary_id[64];
    bl_glue_binary_id(argv0, binary_id, sizeof(binary_id));
    int n = snprintf(out, cap,
        "%s/.brightchain/iputils-pins/%s.sep-pub", home, binary_id);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

/* Delete this binary's pin file. Returns 0 on success (or if the file
 * was already absent), -1 on real I/O error. Always prints a one-line
 * status to stderr. */
static inline int bl_glue_reset_pin(const char *argv0)
{
    char path[1024];
    if (bl_glue_pin_path(argv0, path, sizeof(path)) != 0) {
        fprintf(stderr, "brightlink: could not resolve pin path\n");
        return -1;
    }
    if (unlink(path) != 0) {
        if (errno == ENOENT) {
            fprintf(stderr, "brightlink: no pin to remove at %s\n", path);
            return 0;
        }
        fprintf(stderr, "brightlink: failed to remove %s: %s\n",
                path, strerror(errno));
        return -1;
    }
    fprintf(stderr,
        "brightlink: pin removed: %s\n"
        "  The next bridge handshake will TOFU-pin the new key.\n", path);
    return 0;
}

/* Scan argv for the global --reset-brightlink-pin flag; if present,
 * delete the pin file for this binary and exit. Tools call this once,
 * at the top of main(), so the flag works uniformly without each tool
 * having to thread it through its own argument parser. */
static inline void bl_glue_handle_global_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--reset-brightlink-pin") == 0) {
            int rc = bl_glue_reset_pin(argv[0]);
            exit(rc == 0 ? 0 : 1);
        }
    }
}

/*
 * Populate (e, *have_ecef, g) from BrightNexus, leaving them unchanged
 * on any failure (caller falls through to geoIP). Mirrors the previous
 * bl_get_geo signature so the call sites in bping.c et al. stay terse.
 */
static inline void bl_glue_get_geo(const char *argv0,
                                   bs_ecef_t *e, int *have_ecef, bs_geo_t *g)
{
    /* Skip if coords already provided via CLI flags — BSPACE priority. */
    if (*have_ecef || g->valid) return;

    const char *home = bl_glue_home();
    if (home == NULL) return;

    /* Pin dir under $HOME/.brightchain/iputils-pins/. The library will
     * mkdir(0700) the leaf dir; we have to make sure ~/.brightchain
     * itself exists. */
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s/.brightchain", home);
    if (mkdir(parent, 0700) != 0 && errno != EEXIST) return;

    char pin_dir[1024];
    snprintf(pin_dir, sizeof(pin_dir), "%s/.brightchain/iputils-pins", home);

    char binary_id[64];
    bl_glue_binary_id(argv0, binary_id, sizeof(binary_id));

    bl_pin_store_t *store = bl_pin_store_file(pin_dir, binary_id);
    if (store == NULL) return;

    int debug_on = (getenv("BRIGHTLINK_DEBUG") != NULL);
    bl_client_config_t cfg = {
        .agent_name   = binary_id,
        .pin_store    = store,
        .debug_stream = debug_on ? stderr : NULL,
    };
    bl_client_t *c = bl_client_new(&cfg);
    if (c == NULL) {
        bl_pin_store_free(store);
        return;
    }

    bl_geo_position_t pos;
    bl_status_t st = bl_geo_get(c, BL_GEO_FORMAT_BOTH, &pos);
    if (st == BL_OK) {
        /* Clear any stale reason from a prior call. */
        bl_glue_reason_buf()[0] = '\0';
        if (pos.have_wgs84) {
            g->lat = pos.wgs84_lat;
            g->lon = pos.wgs84_lon;
            g->valid = 1;
            snprintf(g->tag, sizeof(g->tag), "[brightlink]");
        }
        if (pos.have_brightspace) {
            e->x = pos.brightspace_x_bm;
            e->y = pos.brightspace_y_bm;
            e->z = pos.brightspace_z_bm;
            *have_ecef = 1;
            snprintf(g->tag, sizeof(g->tag), "[brightlink:ecef]");
        }
    } else {
        /* Record a short slug describing the failure so the geoIP path
         * can annotate its output tag. Keep these short — they must fit
         * inside "[~geoIP/<slug>]" within bs_geo_t.tag. */
        const char *slug = NULL;
        switch (st) {
        case BL_ERR_PIN_MISMATCH:   slug = "pin-mismatch"; break;
        case BL_ERR_CRYPTO:         slug = "crypto";       break;
        case BL_ERR_PROTOCOL:       slug = "proto";        break;
        case BL_ERR_BRIDGE_REFUSED: slug = "refused";      break;
        case BL_ERR_DENIED:         slug = "denied";       break;
        case BL_ERR_TIMEOUT:        slug = "timeout";      break;
        case BL_ERR_TRANSPORT:      slug = NULL;           break;
        default:                    slug = NULL;           break;
        }
        if (slug != NULL)
            snprintf(bl_glue_reason_buf(), 16, "%s", slug);
        else
            bl_glue_reason_buf()[0] = '\0';

        if (getenv("BRIGHTLINK_QUIET") == NULL) {
            /* Surface security-relevant failures to stderr without requiring
             * BRIGHTLINK_DEBUG=1. Transport/timeout failures (bridge simply
             * isn't running) stay silent — that's the expected geoIP path. */
            switch (st) {
            case BL_ERR_PIN_MISMATCH:
                fprintf(stderr,
                    "brightlink: WARNING: pinned BrightNexus key does not match the\n"
                    "  bridge currently listening. Either the bridge was reinstalled\n"
                    "  (key rotated), you're talking to a different bridge, or this\n"
                    "  is a MITM attempt. Falling back to geoIP for this run.\n"
                    "  If you trust the new bridge, clear the pin and retry:\n"
                    "    %s --reset-brightlink-pin\n"
                    "  or remove the pin file directly:\n"
                    "    rm %s/%s.sep-pub\n"
                    "  (Suppress this notice with BRIGHTLINK_QUIET=1.)\n",
                    (argv0 && *argv0) ? argv0 : "<tool>", pin_dir, binary_id);
                break;
            case BL_ERR_CRYPTO:
            case BL_ERR_PROTOCOL:
                fprintf(stderr,
                    "brightlink: WARNING: bridge handshake failed (%s). Falling back\n"
                    "  to geoIP. Run with BRIGHTLINK_DEBUG=1 for details.\n",
                    bl_strerror(st));
                break;
            case BL_ERR_BRIDGE_REFUSED:
            case BL_ERR_DENIED:
                fprintf(stderr,
                    "brightlink: bridge refused geo request (%s). Falling back to geoIP.\n",
                    bl_strerror(st));
                break;
            case BL_ERR_TRANSPORT:
            case BL_ERR_TIMEOUT:
            default:
                /* Silent: no bridge running, or transient I/O. */
                break;
            }
        }
    }

    bl_client_free(c);
}

#endif /* BRIGHTLINK_GLUE_H */
