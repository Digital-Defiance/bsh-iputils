#ifndef IPUTILS_COLOR_H
#define IPUTILS_COLOR_H

#include <stdio.h>

typedef enum {
	IPU_COLOR_AUTO,
	IPU_COLOR_ALWAYS,
	IPU_COLOR_NEVER,
	IPU_COLOR_ANSI,
	IPU_COLOR_TRUECOLOR,
} ipu_color_when_t;

typedef enum {
	IPU_SCHEME_DEFAULT,
	IPU_SCHEME_BRIGHT,
} ipu_color_scheme_t;

typedef struct {
	int enabled;
	const char *reset;
	const char *title;
	const char *label;
	const char *detail;
	const char *value;
	const char *unit;
	const char *hop;
	const char *host;
	const char *location;
	const char *tag;
	const char *header;
	const char *rtt;
	const char *ok;
	const char *warn;
	const char *bad;
} ipu_colors_t;

extern ipu_colors_t ipu_colors;

#define IPU_COLOR_USAGE \
	"  --color[=WHEN]           Colorize output: auto, always, never, ansi, truecolor\n" \
	"  --no-color               Disable color output\n" \
	"  --color-scheme=SCHEME    Palette: default or bright\n"

#define IPU(c, field) ((c)->enabled ? (c)->field : "")

void ipu_color_init(int *argc, char **argv);
int ipu_color_parse_when(const char *value, ipu_color_when_t *out);
int ipu_color_parse_scheme(const char *value, ipu_color_scheme_t *out);
void ipu_fprint_label(FILE *fp, const ipu_colors_t *c,
		      const char *style, const char *name, int width);
const char *ipu_efficiency_style(const ipu_colors_t *c, double efficiency_pct);
const char *ipu_loss_style(const ipu_colors_t *c, double loss_pct);
const char *ipu_in_ring_style(const ipu_colors_t *c, int in_ring);

#endif /* IPUTILS_COLOR_H */
