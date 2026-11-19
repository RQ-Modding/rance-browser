/* The games' profiles: games/<code>.tsv, built into the program (tools/embed_games.py).
 *
 * A profile adds what a game's files do not say: its title, whether the browser was checked on it,
 * labels for its CG folders or number ranges, its font. A game without one still opens, marked
 * "not tested". The format is in games/README.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"

#include "core.h"

struct game_src { const char *code; const char *text; unsigned len; };
extern const struct game_src game_srcs[];

static struct profile *profiles;
static int nprofiles = -1;

/* Splits a line at its tabs, in place. */
static int fields(char *line, char **out, int max)
{
	int n = 0;
	char *s = line;
	while (n < max) {
		out[n++] = s;
		char *tab = strchr(s, '\t');
		if (!tab)
			break;
		*tab = 0;
		s = tab + 1;
	}
	return n;
}

static bool yes(const char *v)
{
	return !strcmp(v, "yes") || !strcmp(v, "1") || !strcmp(v, "true");
}

static void parse(struct profile *p, const struct game_src *src)
{
	memset(p, 0, sizeof *p);
	p->code = src->code;
	const char *s = src->text, *end = s + src->len;
	while (s < end) {
		const char *nl = memchr(s, '\n', end - s);
		size_t len = (nl ? nl : end) - s;
		char *line = xmalloc(len + 1);
		memcpy(line, s, len);
		line[len] = 0;
		s = nl ? nl + 1 : end;
		if (len && line[len - 1] == '\r')
			line[len - 1] = 0;
		char *f[4];
		int n = line[0] && line[0] != '#' ? fields(line, f, 4) : 0;
		if (n >= 2 && !strcmp(f[0], "title"))
			p->title = xstrdup(f[1]);
		else if (n >= 2 && !strcmp(f[0], "tested"))
			p->tested = yes(f[1]);
		else if (n >= 2 && !strcmp(f[0], "rq"))
			p->rq = yes(f[1]);
		else if (n >= 2 && !strcmp(f[0], "font"))
			p->font = xstrdup(f[1]);
		else if (n >= 3 && !strcmp(f[0], "folder") && p->nfolders < 64) {
			p->folders[p->nfolders].name = xstrdup(f[1]);
			p->folders[p->nfolders].label = xstrdup(f[2]);
			p->nfolders++;
		} else if (n >= 4 && !strcmp(f[0], "mask") && p->nmasks < 8) {
			p->masks[p->nmasks].lo = atoi(f[1]);
			p->masks[p->nmasks].hi = atoi(f[2]);
			p->masks[p->nmasks].offset = atoi(f[3]);
			p->nmasks++;
		} else if (n >= 4 && !strcmp(f[0], "range") && p->nranges < 128) {
			p->ranges[p->nranges].lo = atoi(f[1]);
			p->ranges[p->nranges].hi = atoi(f[2]);
			p->ranges[p->nranges].label = xstrdup(f[3]);
			p->nranges++;
		}
		free(line);
	}
}

const struct profile *profile_find(const char *code)
{
	if (nprofiles < 0) {
		nprofiles = 0;
		while (game_srcs[nprofiles].code)
			nprofiles++;
		profiles = xcalloc(nprofiles ? nprofiles : 1, sizeof *profiles);
		for (int i = 0; i < nprofiles; i++)
			parse(&profiles[i], &game_srcs[i]);
	}
	for (int i = 0; i < nprofiles; i++)
		if (!strcasecmp(profiles[i].code, code))
			return &profiles[i];
	return NULL;
}
