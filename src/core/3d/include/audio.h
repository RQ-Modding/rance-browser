/* In the place of RQ MOD's include/audio.h: only the effects of the engines after Rance Quest's
 * (.3de files) play a sound, so the browser's view plays none (rb3d.c: calls that do nothing). */
#ifndef SYSTEM4_AUDIO_H
#define SYSTEM4_AUDIO_H

#define AUDIO_NO_METADATA -1

struct archive_data;

int wav_unprepare(int id);
int wav_play(int id);
int wav_stop(int id);
int wav_seek(int id, int pos);
int wav_get_unused_channel(void);
int wav_prepare_from_archive_data(int id, int metadata_no, struct archive_data *dfile);

#endif /* SYSTEM4_AUDIO_H */
