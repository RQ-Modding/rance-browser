/* What every tab shares: the status message, the Lua line with Copy, search, the arrow keys, and
 * the batch save that writes a few files each frame behind a progress window. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "browser.h"

struct ui UI = { .want_tab = -1, .shot_select = -1 };

const char *const tab_keys[TAB_COUNT] = { "cgs", "flash", "music", "sounds", "3d", "textures", "pictures", "dungeons",
                                          "names" };

void ui_status(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(UI.status, sizeof UI.status, fmt, ap);
	va_end(ap);
	UI.status_until = igGetCurrentContext() ? igGetTime() + 6.0 : 6.0;
}

void ui_copy(const char *text)
{
	SDL_SetClipboardText(text);
	ui_status("Copied: %s", text);
}

/* A Lua line the creator can select, with a Copy button. */
void ui_lua_line(const char *id, const char *line)
{
	char buf[2048];
	snprintf(buf, sizeof buf, "%s", line);
	igPushID_Str(id);
	igSetNextItemWidth(igGetContentRegionAvail().x - S(56) - igGetStyle()->ItemSpacing.x);
	igInputText("##lua", buf, sizeof buf, ImGuiInputTextFlags_ReadOnly, NULL, NULL);
	igSameLine(0, -1);
	if (igButton("Copy", v2(S(56), 0)))
		ui_copy(line);
	igPopID();
}

void ui_search_box(const char *id, const char *hint, char *buf, size_t n)
{
	igSetNextItemWidth(-1);
	igInputTextWithHint(id, hint ? hint : "Search: a number or a name", buf, n, 0, NULL, NULL);
}

/* Case-insensitive for ASCII; other text (Japanese) as it is. */
bool ui_match(const char *text, const char *needle)
{
	if (!needle || !*needle)
		return true;
	if (!text)
		return false;
	size_t nl = strlen(needle);
	for (const char *s = text; *s; s++) {
		size_t i = 0;
		while (i < nl && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)needle[i]))
			i++;
		if (i == nl)
			return true;
	}
	return false;
}

static bool all_digits(const char *s)
{
	if (!*s)
		return false;
	for (; *s; s++)
		if (!isdigit((unsigned char)*s))
			return false;
	return true;
}

/* A number typed in the search box matches the numbers that start with it. */
bool ui_number_match(int no, const char *needle)
{
	if (!all_digits(needle))
		return false;
	char s[16];
	snprintf(s, sizeof s, "%d", no);
	return !strncmp(s, needle, strlen(needle));
}

/* Up, Down, Left, Right, Page Up, Page Down, Home and End move along a list, unless a text field
 * takes the keys or a popup (a folder or kind list) is open over it. sel is a position in the
 * list. */
bool ui_nav_keys(int *sel, int count)
{
	ImGuiIO *io = igGetIO_Nil();
	if (io->WantTextInput || count <= 0 || batch_running() || igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopup))
		return false;
	int s = *sel;
	if (igIsKeyPressed_Bool(ImGuiKey_DownArrow, true) || igIsKeyPressed_Bool(ImGuiKey_RightArrow, true))
		s++;
	if (igIsKeyPressed_Bool(ImGuiKey_UpArrow, true) || igIsKeyPressed_Bool(ImGuiKey_LeftArrow, true))
		s--;
	if (igIsKeyPressed_Bool(ImGuiKey_PageDown, true))
		s += 10;
	if (igIsKeyPressed_Bool(ImGuiKey_PageUp, true))
		s -= 10;
	if (igIsKeyPressed_Bool(ImGuiKey_Home, false))
		s = 0;
	if (igIsKeyPressed_Bool(ImGuiKey_End, false))
		s = count - 1;
	if (s < 0) s = 0;
	if (s >= count) s = count - 1;
	if (s == *sel)
		return false;
	*sel = s;
	return true;
}

/* ---- batch saves ---- */

static struct {
	bool on;
	char what[128];
	char dir[1024];
	int count, i, ok;
	batch_fn fn;
	bool open_popup;
} B;

void batch_start(const char *what, int count, batch_fn fn, const char *dir)
{
	memset(&B, 0, sizeof B);
	B.on = count > 0;
	B.count = count;
	B.fn = fn;
	B.open_popup = true;
	snprintf(B.what, sizeof B.what, "%s", what);
	snprintf(B.dir, sizeof B.dir, "%s", dir);
}

bool batch_running(void)
{
	return B.on;
}

void batch_frame(void)
{
	if (!B.on)
		return;
	if (B.open_popup) {
		igOpenPopup_Str("Saving", 0);
		B.open_popup = false;
	}
	Uint64 t0 = SDL_GetTicks64();
	while (B.i < B.count && SDL_GetTicks64() - t0 < 30) {
		if (B.fn(B.i, B.dir))
			B.ok++;
		B.i++;
	}
	bool cancel = false;
	igSetNextWindowSize(v2(S(460), 0), ImGuiCond_Always);
	if (igBeginPopupModal("Saving", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
		igTextWrapped("%s", B.what);
		igTextDisabled("%s", B.dir);
		char over[64];
		snprintf(over, sizeof over, "%d of %d", B.i, B.count);
		igProgressBar(B.count ? (float)B.i / B.count : 1.0f, v2(-1, 0), over);
		if (igButton("Cancel", v2(S(100), 0)))
			cancel = true;
		if (B.i >= B.count || cancel)
			igCloseCurrentPopup();
		igEndPopup();
	}
	if (B.i >= B.count || cancel) {
		ui_status("Saved %d of %d files in %s%s", B.ok, B.count, B.dir, cancel ? " (cancelled)" : "");
		B.on = false;
	}
}
