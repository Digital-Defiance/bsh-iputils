#define _GNU_SOURCE

#include "iputils_color.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ipu_colors_t ipu_colors;

static const ipu_colors_t ipu_colors_off = {
	.enabled = 0,
	.reset = "",
	.title = "",
	.label = "",
	.detail = "",
	.value = "",
	.unit = "",
	.hop = "",
	.host = "",
	.location = "",
	.tag = "",
	.header = "",
	.rtt = "",
	.ok = "",
	.warn = "",
	.bad = "",
};

static const ipu_colors_t ipu_colors_ansi_default = {
	.enabled = 1,
	.reset = "\x1b[0m",
	.title = "\x1b[1;36m",
	.label = "\x1b[36m",
	.detail = "\x1b[2m",
	.value = "\x1b[1m",
	.unit = "\x1b[2m",
	.hop = "\x1b[35m",
	.host = "\x1b[1m",
	.location = "\x1b[32m",
	.tag = "\x1b[34m",
	.header = "\x1b[2;34m",
	.rtt = "\x1b[1;36m",
	.ok = "\x1b[32m",
	.warn = "\x1b[33m",
	.bad = "\x1b[1;31m",
};

static const ipu_colors_t ipu_colors_ansi_bright = {
	.enabled = 1,
	.reset = "\x1b[0m",
	.title = "\x1b[1;96m",
	.label = "\x1b[96m",
	.detail = "\x1b[2m",
	.value = "\x1b[1;97m",
	.unit = "\x1b[2m",
	.hop = "\x1b[95m",
	.host = "\x1b[1;97m",
	.location = "\x1b[92m",
	.tag = "\x1b[94m",
	.header = "\x1b[2;94m",
	.rtt = "\x1b[1;96m",
	.ok = "\x1b[92m",
	.warn = "\x1b[93m",
	.bad = "\x1b[1;91m",
};

static const ipu_colors_t ipu_colors_truecolor_default = {
	.enabled = 1,
	.reset = "\x1b[0m",
	.title = "\x1b[1;38;2;80;200;220m",
	.label = "\x1b[38;2;80;200;220m",
	.detail = "\x1b[2;38;2;120;120;120m",
	.value = "\x1b[1;38;2;240;240;240m",
	.unit = "\x1b[2;38;2;128;128;128m",
	.hop = "\x1b[38;2;200;120;220m",
	.host = "\x1b[1;38;2;240;240;240m",
	.location = "\x1b[38;2;120;220;140m",
	.tag = "\x1b[38;2;120;160;240m",
	.header = "\x1b[2;38;2;120;160;240m",
	.rtt = "\x1b[1;38;2;80;200;220m",
	.ok = "\x1b[38;2;100;200;100m",
	.warn = "\x1b[38;2;240;200;80m",
	.bad = "\x1b[38;2;240;100;100m",
};

static const ipu_colors_t ipu_colors_truecolor_bright = {
	.enabled = 1,
	.reset = "\x1b[0m",
	.title = "\x1b[1;38;2;0;220;255m",
	.label = "\x1b[38;2;0;220;255m",
	.detail = "\x1b[2;38;2;160;160;160m",
	.value = "\x1b[1;38;2;255;255;255m",
	.unit = "\x1b[2;38;2;160;160;160m",
	.hop = "\x1b[38;2;255;120;255m",
	.host = "\x1b[1;38;2;255;255;255m",
	.location = "\x1b[38;2;80;255;120m",
	.tag = "\x1b[38;2;120;180;255m",
	.header = "\x1b[2;38;2;120;180;255m",
	.rtt = "\x1b[1;38;2;0;220;255m",
	.ok = "\x1b[38;2;80;255;120m",
	.warn = "\x1b[38;2;255;220;80m",
	.bad = "\x1b[38;2;255;80;80m",
};

static int strcaseeq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
	}
	return *a == *b;
}

static int env_truthy(const char *name)
{
	const char *v = getenv(name);

	if (v == NULL || v[0] == '\0')
		return 0;
	return strcmp(v, "0") != 0 && strcaseeq(v, "false") == 0;
}

static int no_color_requested(void)
{
	const char *clicolor = getenv("CLICOLOR");

	if (getenv("NO_COLOR") != NULL)
		return 1;
	if (clicolor != NULL && (strcmp(clicolor, "0") == 0 || strcaseeq(clicolor, "false")))
		return 1;
	return 0;
}

static int term_supports_truecolor(void)
{
	const char *colorterm = getenv("COLORTERM");
	const char *term = getenv("TERM");

	if (colorterm != NULL) {
		if (strcaseeq(colorterm, "truecolor") || strcaseeq(colorterm, "24bit"))
			return 1;
	}
	if (term != NULL && strstr(term, "truecolor") != NULL)
		return 1;
	return 0;
}

static int preferred_truecolor(void)
{
	return term_supports_truecolor();
}

static const ipu_colors_t *palette_ansi(ipu_color_scheme_t scheme)
{
	return scheme == IPU_SCHEME_BRIGHT ?
		&ipu_colors_ansi_bright : &ipu_colors_ansi_default;
}

static const ipu_colors_t *palette_truecolor(ipu_color_scheme_t scheme)
{
	return scheme == IPU_SCHEME_BRIGHT ?
		&ipu_colors_truecolor_bright : &ipu_colors_truecolor_default;
}

static void ipu_colors_resolve(ipu_color_when_t when, ipu_color_scheme_t scheme)
{
	if (when == IPU_COLOR_NEVER) {
		ipu_colors = ipu_colors_off;
		return;
	}

	if (when == IPU_COLOR_AUTO && no_color_requested()) {
		ipu_colors = ipu_colors_off;
		return;
	}

	if (when == IPU_COLOR_ANSI) {
		ipu_colors = *palette_ansi(scheme);
		return;
	}

	if (when == IPU_COLOR_TRUECOLOR) {
		ipu_colors = *palette_truecolor(scheme);
		return;
	}

	if (when == IPU_COLOR_ALWAYS || when == IPU_COLOR_AUTO) {
		if (when == IPU_COLOR_AUTO &&
		    !isatty(fileno(stdout)) && !env_truthy("CLICOLOR_FORCE")) {
			ipu_colors = ipu_colors_off;
			return;
		}
		if (preferred_truecolor())
			ipu_colors = *palette_truecolor(scheme);
		else
			ipu_colors = *palette_ansi(scheme);
		return;
	}

	ipu_colors = ipu_colors_off;
}

int ipu_color_parse_when(const char *value, ipu_color_when_t *out)
{
	if (value == NULL || value[0] == '\0')
		return -1;

	if (strcaseeq(value, "auto"))
		*out = IPU_COLOR_AUTO;
	else if (strcaseeq(value, "always") || strcaseeq(value, "on") ||
		 strcaseeq(value, "yes"))
		*out = IPU_COLOR_ALWAYS;
	else if (strcaseeq(value, "never") || strcaseeq(value, "off") ||
		 strcaseeq(value, "no"))
		*out = IPU_COLOR_NEVER;
	else if (strcaseeq(value, "ansi") || strcaseeq(value, "16"))
		*out = IPU_COLOR_ANSI;
	else if (strcaseeq(value, "truecolor") || strcaseeq(value, "24bit") ||
		 strcaseeq(value, "rgb"))
		*out = IPU_COLOR_TRUECOLOR;
	else
		return -1;

	return 0;
}

int ipu_color_parse_scheme(const char *value, ipu_color_scheme_t *out)
{
	if (value == NULL || value[0] == '\0')
		return -1;

	if (strcaseeq(value, "default"))
		*out = IPU_SCHEME_DEFAULT;
	else if (strcaseeq(value, "bright"))
		*out = IPU_SCHEME_BRIGHT;
	else
		return -1;

	return 0;
}

static void color_usage(const char *prog)
{
	fprintf(stderr,
		"%s: invalid color setting (expected auto, always, never, ansi, or truecolor)\n",
		prog);
}

static int parse_color_arg(const char *prog, const char *arg,
			   ipu_color_when_t *when)
{
	const char *value = arg;

	if (strncmp(arg, "--color=", 8) == 0)
		value = arg + 8;
	else if (strcmp(arg, "--color") == 0)
		value = "always";

	if (ipu_color_parse_when(value, when) != 0) {
		color_usage(prog);
		exit(2);
	}
	return 1;
}

static int parse_scheme_arg(const char *prog, const char *arg,
			    ipu_color_scheme_t *scheme)
{
	const char *value = arg;

	if (strncmp(arg, "--color-scheme=", 15) == 0)
		value = arg + 15;

	if (ipu_color_parse_scheme(value, scheme) != 0) {
		fprintf(stderr,
			"%s: invalid color scheme (expected default or bright)\n",
			prog);
		exit(2);
	}
	return 1;
}

static void compact_argv(int *argc, char **argv, int remove_at)
{
	int i;

	for (i = remove_at; i + 1 < *argc; i++)
		argv[i] = argv[i + 1];
	(*argc)--;
	argv[*argc] = NULL;
}

void ipu_color_init(int *argc, char **argv)
{
	ipu_color_when_t when = IPU_COLOR_AUTO;
	ipu_color_scheme_t scheme = IPU_SCHEME_DEFAULT;
	const char *prog = (argv && argv[0]) ? argv[0] : "iputils";
	const char *env_color = getenv("IPUTILS_COLOR");
	const char *env_scheme = getenv("IPUTILS_COLOR_SCHEME");
	int i = 1;

	if (env_color != NULL && env_color[0] != '\0') {
		if (ipu_color_parse_when(env_color, &when) != 0)
			color_usage(prog);
	}
	if (env_scheme != NULL && env_scheme[0] != '\0') {
		if (ipu_color_parse_scheme(env_scheme, &scheme) != 0) {
			fprintf(stderr,
				"%s: invalid IPUTILS_COLOR_SCHEME (expected default or bright)\n",
				prog);
			exit(2);
		}
	}

	while (i < *argc) {
		if (strcmp(argv[i], "--no-color") == 0) {
			when = IPU_COLOR_NEVER;
			compact_argv(argc, argv, i);
			continue;
		}
		if (strncmp(argv[i], "--color=", 8) == 0 ||
		    strcmp(argv[i], "--color") == 0) {
			parse_color_arg(prog, argv[i], &when);
			compact_argv(argc, argv, i);
			continue;
		}
		if (strncmp(argv[i], "--color-scheme=", 15) == 0) {
			parse_scheme_arg(prog, argv[i], &scheme);
			compact_argv(argc, argv, i);
			continue;
		}
		i++;
	}

	ipu_colors_resolve(when, scheme);
}

void ipu_fprint_label(FILE *fp, const ipu_colors_t *c,
		      const char *style, const char *name, int width)
{
	if (c->enabled)
		fprintf(fp, "%s%-*s%s", style, width, name, c->reset);
	else
		fprintf(fp, "%-*s", width, name);
}

const char *ipu_efficiency_style(const ipu_colors_t *c, double efficiency_pct)
{
	if (!c->enabled)
		return "";
	if (efficiency_pct > 100.0)
		return c->warn;
	return c->ok;
}

const char *ipu_loss_style(const ipu_colors_t *c, double loss_pct)
{
	if (!c->enabled)
		return "";
	if (loss_pct >= 50.0)
		return c->bad;
	if (loss_pct >= 10.0)
		return c->warn;
	return c->value;
}

const char *ipu_in_ring_style(const ipu_colors_t *c, int in_ring)
{
	if (!c->enabled)
		return "";
	return in_ring ? c->ok : c->bad;
}
