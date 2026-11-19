/* The player: one sound at a time, read with libsndfile from memory and played through SDL.
 * A loop as the game's .bgi gives it: from loop_end back to loop_start, in sample frames. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sndfile.h>

#include "system4.h"
#include "core.h"

struct vio {
	uint8_t *data;
	sf_count_t size, pos;
};

static sf_count_t vio_len(void *u) { return ((struct vio *)u)->size; }
static sf_count_t vio_tell(void *u) { return ((struct vio *)u)->pos; }

static sf_count_t vio_seek(sf_count_t off, int whence, void *u)
{
	struct vio *v = u;
	sf_count_t p = whence == SEEK_SET ? off : whence == SEEK_CUR ? v->pos + off : v->size + off;
	if (p < 0) p = 0;
	if (p > v->size) p = v->size;
	v->pos = p;
	return p;
}

static sf_count_t vio_read(void *ptr, sf_count_t n, void *u)
{
	struct vio *v = u;
	if (n > v->size - v->pos)
		n = v->size - v->pos;
	memcpy(ptr, v->data + v->pos, n);
	v->pos += n;
	return n;
}

static sf_count_t vio_write(const void *ptr, sf_count_t n, void *u)
{
	(void)ptr; (void)n; (void)u;
	return 0;
}

static SF_VIRTUAL_IO vio_ops = { vio_len, vio_seek, vio_read, vio_write, vio_tell };

static struct {
	SDL_AudioDeviceID dev;
	SNDFILE *f;
	SF_INFO info;
	struct vio v;
	bool playing;
	bool loop;
	int64_t loop_start, loop_end;
	int64_t pos;
	float volume;
	char title[256];
} P = { .volume = 0.8f };

static void callback(void *u, Uint8 *stream, int len)
{
	(void)u;
	float *out = (float *)stream;
	int ch = P.info.channels > 0 ? P.info.channels : 1;
	int frames = len / (int)(sizeof(float) * ch);
	int filled = 0, guard = 0;
	while (P.playing && P.f && filled < frames && guard++ < 8) {
		sf_count_t want = frames - filled;
		bool looping = P.loop && P.loop_end > P.loop_start;
		if (looping && P.pos + want > P.loop_end)
			want = P.loop_end - P.pos;
		sf_count_t got = want > 0 ? sf_readf_float(P.f, out + (size_t)filled * ch, want) : 0;
		if (got > 0) {
			P.pos += got;
			filled += (int)got;
			guard = 0;
		}
		if (looping && P.pos >= P.loop_end) {
			sf_seek(P.f, P.loop_start, SEEK_SET);
			P.pos = P.loop_start;
		} else if (got < want || got <= 0) {
			/* The end: back to the start, stopped. */
			sf_seek(P.f, 0, SEEK_SET);
			P.pos = 0;
			P.playing = false;
		}
	}
	for (int i = 0; i < filled * ch; i++)
		out[i] *= P.volume;
	memset(out + (size_t)filled * ch, 0, (size_t)(frames - filled) * ch * sizeof(float));
}

void player_init(void)
{
}

void player_close(void)
{
	if (P.dev) {
		SDL_CloseAudioDevice(P.dev);
		P.dev = 0;
	}
	if (P.f) {
		sf_close(P.f);
		P.f = NULL;
	}
	free(P.v.data);
	memset(&P.v, 0, sizeof P.v);
	memset(&P.info, 0, sizeof P.info);
	P.playing = false;
	P.loop = false;
	P.pos = 0;
	P.title[0] = 0;
}

static bool open_device(void)
{
	SDL_AudioSpec want = { 0 }, have;
	want.freq = P.info.samplerate;
	want.format = AUDIO_F32SYS;
	want.channels = (Uint8)P.info.channels;
	want.samples = 2048;
	want.callback = callback;
	P.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!P.dev) {
		core_status("No sound device: %s", SDL_GetError());
		return false;
	}
	SDL_PauseAudioDevice(P.dev, 0);
	return true;
}

/* Takes a copy of data. */
static bool load(const uint8_t *data, size_t size, const char *title, SF_INFO *info)
{
	player_close();
	P.v.data = xmalloc(size ? size : 1);
	memcpy(P.v.data, data, size);
	P.v.size = (sf_count_t)size;
	P.info = *info;
	P.f = sf_open_virtual(&vio_ops, SFM_READ, &P.info, &P.v);
	if (!P.f || P.info.channels <= 0 || P.info.samplerate <= 0) {
		core_status("Could not read the sound: %s", sf_strerror(P.f));
		player_close();
		return false;
	}
	snprintf(P.title, sizeof P.title, "%s", title);
	open_device();
	return true;
}

bool player_load(const uint8_t *data, size_t size, const char *title)
{
	SF_INFO info = { 0 };
	return load(data, size, title, &info);
}

bool player_load_pcm(const uint8_t *pcm, size_t len, int rate, int channels, int bits, const char *title)
{
	SF_INFO info = { 0 };
	info.samplerate = rate;
	info.channels = channels;
	info.format = SF_FORMAT_RAW | SF_ENDIAN_LITTLE | (bits == 16 ? SF_FORMAT_PCM_16 : SF_FORMAT_PCM_U8);
	return load(pcm, len, title, &info);
}

void player_set_loop(bool on, int start, int end)
{
	if (P.dev) SDL_LockAudioDevice(P.dev);
	P.loop = on;
	P.loop_start = start < 0 ? 0 : start;
	P.loop_end = end <= 0 || end > P.info.frames ? P.info.frames : end;
	if (P.dev) SDL_UnlockAudioDevice(P.dev);
}

void player_play(void)
{
	if (!P.f || !P.dev)
		return;
	SDL_LockAudioDevice(P.dev);
	P.playing = true;
	SDL_UnlockAudioDevice(P.dev);
}

void player_pause(void)
{
	if (!P.dev)
		return;
	SDL_LockAudioDevice(P.dev);
	P.playing = false;
	SDL_UnlockAudioDevice(P.dev);
}

void player_stop(void)
{
	if (!P.dev)
		return;
	SDL_LockAudioDevice(P.dev);
	P.playing = false;
	if (P.f)
		sf_seek(P.f, 0, SEEK_SET);
	P.pos = 0;
	SDL_UnlockAudioDevice(P.dev);
}

void player_seek(int64_t frame)
{
	if (!P.f)
		return;
	if (frame < 0) frame = 0;
	if (frame > P.info.frames) frame = P.info.frames;
	if (P.dev) SDL_LockAudioDevice(P.dev);
	sf_seek(P.f, frame, SEEK_SET);
	P.pos = frame;
	if (P.dev) SDL_UnlockAudioDevice(P.dev);
}

bool player_playing(void) { return P.playing; }
bool player_loaded(void) { return P.f != NULL; }
int64_t player_pos(void) { return P.pos; }
int64_t player_frames(void) { return P.info.frames; }
int player_rate(void) { return P.info.samplerate; }
int player_channels(void) { return P.info.channels; }
void player_set_volume(float v) { P.volume = v < 0 ? 0 : v > 1 ? 1 : v; }
float player_volume(void) { return P.volume; }
const char *player_title(void) { return P.title; }

/* For the check: a file's length from its header, or with full, every frame decoded into nothing
 * (which proves the whole file reads). */
bool player_probe(const uint8_t *data, size_t size, bool full, int64_t *frames_out, int *rate_out, int *channels_out)
{
	struct vio v = { (uint8_t *)data, (sf_count_t)size, 0 };
	SF_INFO info = { 0 };
	SNDFILE *f = sf_open_virtual(&vio_ops, SFM_READ, &info, &v);
	if (!f)
		return false;
	int64_t total = info.frames;
	if (full) {
		float buf[4096];
		int ch = info.channels > 0 ? info.channels : 1;
		sf_count_t got;
		total = 0;
		while ((got = sf_readf_float(f, buf, 4096 / ch)) > 0)
			total += got;
	}
	sf_close(f);
	if (frames_out) *frames_out = total;
	if (rate_out) *rate_out = info.samplerate;
	if (channels_out) *channels_out = info.channels;
	return total > 0;
}
