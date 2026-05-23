/*
 * brightlink_glue.h — bsh-iputils-side adapter on top of libBrightLink.
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
    }
    /* Anything else: leave caller's outputs untouched. The diagnostic
     * has already been emitted to stderr via debug_stream when enabled. */

    bl_client_free(c);
}

#endif /* BRIGHTLINK_GLUE_H */
