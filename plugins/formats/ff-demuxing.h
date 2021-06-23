#ifndef _PLUGINS_FORMATS_FF_DEMUXING_H_
#define _PLUGINS_FORMATS_FF_DEMUXING_H_

#include "util/codec-def.h"

#ifdef __cplusplus
extern "C" {
#endif

void *ff_demux_create(const char *url, bool save_file);
bool ff_demux_start(void *data, void (*proc_packet)(void *, struct encoder_packet *packet), void *param);

void ff_demux_stop(void *data);
void ff_demux_destroy(void *data);

#ifdef __cplusplus
}
#endif
#endif  //_PLUGINS_FORMATS_FF_DEMUXING_H_