/* Windows: the folder and save dialogs, the program's own folder, a console for --check, and the
 * settings file beside the program (rance-browser.json: the game folder, the last save folder; an
 * RQ Browser's rq-browser.json is read when it is the only one), and starting the program again on
 * another game. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_syswm.h>

#include "system4.h"
#include "system4/file.h"
#include "cJSON.h"

#include "browser.h"

extern SDL_Window *g_window;

static cJSON *settings;
static char settings_path[1100];

static wchar_t *wide(const char *s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	wchar_t *w = xmalloc((n > 0 ? n : 1) * sizeof(wchar_t));
	if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n))
		w[0] = 0;
	return w;
}

static void narrow(const wchar_t *w, char *out, size_t n)
{
	if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL))
		out[0] = 0;
}

static HWND owner(void)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (g_window && SDL_GetWindowWMInfo(g_window, &info))
		return info.info.win.window;
	return NULL;
}

bool win_exe_dir(char *out, size_t n)
{
	wchar_t w[MAX_PATH * 2];
	DWORD len = GetModuleFileNameW(NULL, w, MAX_PATH * 2);
	if (!len || len >= MAX_PATH * 2)
		return false;
	wchar_t *slash = wcsrchr(w, L'\\');
	if (slash)
		*slash = 0;
	narrow(w, out, n);
	for (char *p = out; *p; p++)
		if (*p == '\\')
			*p = '/';
	return out[0] != 0;
}

void win_init(void)
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	char dir[1024];
	if (!win_exe_dir(dir, sizeof dir))
		return;
	snprintf(settings_path, sizeof settings_path, "%s/rance-browser.json", dir);
	size_t size = 0;
	uint8_t *raw = file_read(settings_path, &size);
	if (!raw) {
		/* RQ Browser, the name before 1.0.0, kept the same settings beside it. */
		char old[1100];
		snprintf(old, sizeof old, "%s/rq-browser.json", dir);
		raw = file_read(old, &size);
	}
	if (raw) {
		char *text = xmalloc(size + 1);
		memcpy(text, raw, size);
		text[size] = 0;
		settings = cJSON_Parse(text);
		free(text);
		free(raw);
	}
	if (!cJSON_IsObject(settings)) {
		cJSON_Delete(settings);
		settings = cJSON_CreateObject();
	}
}

bool win_relaunch(const char *game_dir)
{
	wchar_t exe[MAX_PATH * 2];
	DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
	if (!len || len >= MAX_PATH * 2)
		return false;
	wchar_t *dir = wide(game_dir);
	size_t n = wcslen(exe) + wcslen(dir) + 32;
	wchar_t *cmd = xmalloc(n * sizeof(wchar_t));
	_snwprintf(cmd, n, L"\"%ls\" --game \"%ls\"", exe, dir);
	STARTUPINFOW si = { .cb = sizeof si };
	PROCESS_INFORMATION pi;
	bool ok = CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	if (ok) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	free(cmd);
	free(dir);
	return ok;
}

const char *settings_get(const char *key)
{
	cJSON *v = cJSON_GetObjectItemCaseSensitive(settings, key);
	return cJSON_IsString(v) ? v->valuestring : NULL;
}

void settings_set(const char *key, const char *value)
{
	if (!settings || !settings_path[0])
		return;
	cJSON_DeleteItemFromObjectCaseSensitive(settings, key);
	cJSON_AddStringToObject(settings, key, value);
	char *text = cJSON_Print(settings);
	if (text) {
		save_bytes(settings_path, (const uint8_t *)text, strlen(text));
		free(text);
	}
}

void win_last_dir(const char *path)
{
	char dir[1024];
	snprintf(dir, sizeof dir, "%s", path);
	char *a = strrchr(dir, '\\'), *b = strrchr(dir, '/');
	char *s = a > b ? a : b;
	if (s) {
		*s = 0;
		settings_set("save_dir", dir);
	}
}

static int CALLBACK folder_cb(HWND hwnd, UINT msg, LPARAM lp, LPARAM data)
{
	(void)lp;
	if (msg == BFFM_INITIALIZED && data)
		SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
	return 0;
}

bool win_pick_folder(const char *title, char *out, size_t n)
{
	wchar_t *wt = wide(title);
	const char *last = settings_get("save_dir");
	wchar_t *wl = last ? wide(last) : NULL;
	BROWSEINFOW bi = { 0 };
	bi.hwndOwner = owner();
	bi.lpszTitle = wt;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lpfn = folder_cb;
	bi.lParam = (LPARAM)wl;
	PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
	bool ok = false;
	if (pidl) {
		wchar_t path[MAX_PATH * 2];
		if (SHGetPathFromIDListW(pidl, path)) {
			narrow(path, out, n);
			ok = out[0] != 0;
		}
		CoTaskMemFree(pidl);
	}
	free(wt);
	free(wl);
	if (ok)
		settings_set("save_dir", out);
	return ok;
}

bool win_pick_save(const char *title, const char *name, const char *ext, char *out, size_t n)
{
	wchar_t file[MAX_PATH * 2] = { 0 };
	wchar_t *wn = wide(name), *wt = wide(title), *we = wide(ext);
	wcsncpy(file, wn, MAX_PATH * 2 - 1);
	const char *last = settings_get("save_dir");
	wchar_t *wl = last ? wide(last) : NULL;

	/* "PNG file\0*.png\0All files\0*.*\0\0" */
	wchar_t filter[128];
	int k = swprintf(filter, 64, L"%ls file", we);
	k++;
	k += swprintf(filter + k, 32, L"*.%ls", we);
	k++;
	memcpy(filter + k, L"All files\0*.*\0", 15 * sizeof(wchar_t));

	OPENFILENAMEW of = { 0 };
	of.lStructSize = sizeof of;
	of.hwndOwner = owner();
	of.lpstrFilter = filter;
	of.lpstrFile = file;
	of.nMaxFile = MAX_PATH * 2;
	of.lpstrTitle = wt;
	of.lpstrDefExt = we;
	of.lpstrInitialDir = wl;
	of.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	bool ok = GetSaveFileNameW(&of) != 0;
	if (ok)
		narrow(file, out, n);
	free(wn);
	free(wt);
	free(we);
	free(wl);
	if (ok)
		win_last_dir(out);
	return ok;
}

void win_message(const char *title, const char *text)
{
	wchar_t *wt = wide(title), *wx = wide(text);
	MessageBoxW(owner(), wx, wt, MB_OK | MB_ICONINFORMATION);
	free(wt);
	free(wx);
}

/* --check prints to the console it was started from, unless its output already goes somewhere. */
void win_attach_console(void)
{
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN)
		return;
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
	}
}
