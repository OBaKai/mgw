#include "mgw.h"

#include "util/base.h"
#include "buffer/ring-buffer.h"

extern struct mgw_core *mgw;

static bool mgw_source_active(struct mgw_source *source)
{
	return source && source->buffer && source->actived;
}

struct mgw_source_info *get_source_info(const char *id)
{
	for (size_t i = 0; i < mgw->source_types.num; i++) {
		struct mgw_source_info *info = &mgw->source_types.array[i];
		if (strcmp(info->id, id) == 0)
			return info;
	}
	return NULL;
}

bool mgw_source_init_context(struct mgw_source *source,
		mgw_data_t *settings, const char *name, bool is_private)
{
	if (!mgw_context_data_init(&source->context, MGW_OBJ_TYPE_SOURCE,\
				settings, name, is_private))
		return false;
	/** signal notify handlers register */

	return true;
}

mgw_data_t *mgw_source_get_setting(void *src)
{
	mgw_source_t *source = src;
	if (!source)
		return NULL;

	if (source->context.data && !source->private_source)
		return source->info.get_settings(source->context.data);
	else
		return mgw_data_get_obj(source->context.settings, "meta");
}

void mgw_source_set_video_extra_data(mgw_source_t *source,
		uint8_t *extra_data, size_t size)
{
	if (!source)
		return;
	if (source->context.data && !source->private_source)
		return;

	if (source->private_source) {
		bmem_copy(&source->video_header, (const char*)extra_data, size);
	}
}

void mgw_source_set_audio_extra_data(mgw_source_t *source,
		uint8_t channels, uint8_t samplesize, uint32_t samplerate)
{
	size_t header_size = 0;
	uint8_t *header = NULL;

	if (!source)
		return;
	
	if (source->context.data && !source->private_source)
		return;

	if (source->private_source) {
		header_size = mgw_get_aac_lc_header(channels, samplesize, samplerate, &header);
		bmem_copy(&source->audio_header, header, header_size);
        bfree(header);
	}
}

static size_t source_get_header_internal(mgw_source_t *source,
			uint8_t **header, enum encoder_type type)
{
	size_t header_size = 0;
	if (source->context.data && !source->private_source) {
		header_size = source->info.get_extra_data(type, header);

	} else if (source->private_source) {
		if (ENCODER_VIDEO == type) {
			*header = source->video_header.array;
			header_size = source->video_header.len;
		} else {
			*header = source->audio_header.array;
			header_size = source->audio_header.len;
		}
	}
	return header_size;
}

size_t mgw_source_get_video_header(void *source, uint8_t **header)
{
	if (!source || !header)
		return 0;

	return source_get_header_internal((mgw_source_t *)source, header, ENCODER_VIDEO);
}

size_t mgw_source_get_audio_header(void *source, uint8_t **header)
{
	if (!source || !header)
		return 0;

	return source_get_header_internal((mgw_source_t *)source, header, ENCODER_AUDIO);
}

static bool mgw_source_init(struct mgw_source *source)
{
	bool success = false;
	source->control = bzalloc(sizeof(struct mgw_weak_source));
	source->control->source = source;
	
	if (!source->enabled) {
		mgw_data_t *buf_settings = mgw_rb_get_default();
		if (buf_settings) {
			/** aready set by ring buffer default: sort, shared_mem, read_by_time, mem_size, capacity */
			/** need to set: io_mode, name, id */
			mgw_data_set_string(buf_settings, "io_mode", "write");
			mgw_data_set_string(buf_settings, "name", source->context.name);
			mgw_data_set_string(buf_settings, "id", source->info.id);
            bool sort = mgw_data_get_bool(buf_settings, "sort");

            blog(LOG_INFO, "create source buffer with sort: %d", sort);
		}

        struct source_param *param = bzalloc(sizeof(struct source_param));
        param->source = source;
        param->source_settings = mgw_source_get_setting;
        param->audio_header = mgw_source_get_audio_header;
        param->video_header = mgw_source_get_video_header;

		source->buffer = mgw_rb_create(buf_settings, (void*)param);
		if (!source->buffer)
			return false;

		mgw_data_set_obj(source->context.settings, "buffer", buf_settings);
	}

	source->active			= mgw_source_active;
	source->output_packet	= mgw_source_write_packet;
	source->update_settings	= mgw_source_update_settings;

	mgw_source_set_audio_extra_data(source, 2, 16, 44100);
	mgw_data_t *meta = mgw_data_get_obj(source->context.settings, "meta");
	const char *video_payload = mgw_data_get_string(meta, "vencoderID");
	if (!strcmp(video_payload, "avc1"))
		source->video_payload = ENCID_H264;

	mgw_context_data_insert(&source->context, 
					&mgw->data.sources_mutex,
					&mgw->data.first_source);

	return true;
}

static mgw_source_t *mgw_source_create_internal(const char *id,
                const char *name, mgw_data_t *settings, bool is_private)
{
	struct mgw_source *source = bzalloc(sizeof(struct mgw_source));
	const struct mgw_source_info *info = get_source_info(id);
	if (!info) {
		blog(LOG_INFO, "Source ID:%s not found! is private source: %d", id, is_private);
		if (is_private) {
			source->info.id		= bstrdup(id);
			source->private_source	= true;
		}
	} else {
		source->info		= *info;
		source->private_source	= false;
	}

	if (!mgw_source_init_context(source, settings, name, is_private))
		goto failed;

	if (!settings && info)
		info->get_defaults(source->context.settings);

	if (!mgw_source_init(source))
		goto failed;

	if (info) {
		source->context.data = info->create(source->context.settings, source);
		if (!source->context.data) {
			blog(LOG_ERROR, "Failed to create source info: %s", id);
			goto failed;
		}
	}

	source->enabled = true;
	return source;

failed:
	blog(LOG_ERROR, "Create source %s failed!", name);
	mgw_source_destroy(source);
	return NULL;
}

mgw_source_t *mgw_source_create(const char *id,
                const char *name, mgw_data_t *settings)
{
	return mgw_source_create_internal(id, name, settings, false);
}

mgw_source_t *mgw_source_create_private(const char *id, const char *name, mgw_data_t *settings)
{
	return mgw_source_create_internal(id, name, settings, true);
}

void mgw_source_destroy(struct mgw_source *source)
{
	if (!source)
		return;

	blog(LOG_DEBUG, "%s source %s destroyed!",\
			source->context.is_private ? "private" : "",\
			source->context.name);

	if (source->enabled && mgw_source_active(source))
		mgw_source_stop(source);
	
	if (source->context.data) {
		source->info.destroy(source->context.data);
		source->context.data = NULL;
	}
	mgw_context_data_free(&source->context);

	if (source->buffer) {
        struct source_param *param = mgw_rb_get_source_param(source->buffer);
        bfree(param);
		mgw_rb_destroy(source->buffer);
	}

	// if (source->control) {
	// 	bfree(source->control);
	// }
    bmem_free(&source->audio_header);
    bmem_free(&source->video_header);

	if (source->private_source)
		bfree((void*)source->info.id);

	bfree(source);
}

void mgw_source_addref(mgw_source_t *source)
{
	if (!source)
		return;
	mgw_ref_addref(&source->control->ref);
}

void mgw_source_release(mgw_source_t *source)
{
	if (!mgw) {
		blog(LOG_ERROR, "Tried to release a source but mgw core is NULL");
		return;
	}
	if (!source)
		return;

	mgw_weak_source_t *ctrl = source->control;
	if (mgw_ref_release(&ctrl->ref)) {
		mgw_source_destroy(source);
		mgw_weak_source_release(ctrl);
	}
}

void mgw_weak_source_addref(mgw_weak_source_t *weak)
{
	if (!weak)
		return;
	mgw_weak_ref_addref(&weak->ref);
}

void mgw_weak_source_release(mgw_weak_source_t *weak)
{
	if (!weak)
		return;
	if (mgw_weak_ref_release(&weak->ref))
		bfree(weak);
}

mgw_source_t *mgw_source_get_ref(mgw_source_t *source)
{
	if (!source)
		return NULL;
	return mgw_weak_source_get_source(source->control);
}

mgw_weak_source_t *mgw_source_get_weak_source(mgw_source_t *source)
{
	if (!source)
		return NULL;
	mgw_weak_source_t *weak = source->control;
	mgw_weak_source_addref(weak);
	return weak;
}

mgw_source_t *mgw_weak_source_get_source(mgw_weak_source_t *weak)
{
	if (!weak)
		return NULL;

	if (mgw_weak_ref_get_ref(&weak->ref))
		return weak->source;

	return NULL;
}

bool mgw_weak_source_references_source(mgw_weak_source_t *weak,
		mgw_source_t *source)
{
	return weak && source && weak->source == source;
}

void mgw_source_write_packet(mgw_source_t *source, struct encoder_packet *packet)
{
	uint8_t *header = NULL;
	size_t size = 0;
	struct encoder_packet save_packet = {};

    if (!source || !packet || !source->buffer)
		return;

	memcpy(&save_packet, packet, sizeof(save_packet));
	if (ENCODER_VIDEO == packet->type) {
		if (packet->keyframe && ENCID_H264 == source->video_payload) {
			size = mgw_parse_avc_header(&header, packet->data, packet->size);
			if (size > 4 && header) {
				mgw_source_set_video_extra_data(source, header, size);
			}
			bfree(header);

			save_packet.size = mgw_avc_get_keyframe(packet->data, packet->size, &save_packet.data);

			blog(LOG_INFO, "Source save key frame(%d)! data[-3]=%02x, "\
					"data[-2]=%02x, data[-1]=%02x, data[0]=%02x data[1]=%02x",
					save_packet.size ,save_packet.data[-3], save_packet.data[-2],
					save_packet.data[-1], save_packet.data[0], save_packet.data[1]);

		} else if (!packet->keyframe) {
			save_packet.data = (uint8_t*)mgw_avc_find_startcode((const uint8_t*)save_packet.data,
								(const uint8_t*)(save_packet.data + packet->size));
			int8_t start_code_size = mgw_avc_get_startcode_len((const uint8_t*)save_packet.data);
			if (start_code_size >=0) {
				save_packet.data += start_code_size;
				save_packet.size -= start_code_size;
			}
		}
	}

	mgw_rb_write_packet(source->buffer, &save_packet);
}

void mgw_source_update_settings(mgw_source_t *source, mgw_data_t *settings)
{
	if (!source || !settings)
		return;
}


/** --------------------------------------------------------------------- */
/** For local file source and netstream  */

bool mgw_source_start(struct mgw_source *source)
{
	if (!source || !source->enabled || !source->buffer)
		return false;

	if (!source->private_source && !source->context.data)
		return false;

	if (source->actived) {
		mgw_source_stop(source);
		os_event_wait(source->stopping_event);
		source->stop_code = 0;
		source->actived = false;
	}

	source->actived = true;

	if (source->context.data)
		source->actived = source->info.start(source);

	source->actived;
}

void mgw_source_stop(struct mgw_source *source)
{
	if (!source || !source->enabled || !source->buffer)
		return;

	if (!source->private_source && !source->context.data)
		return;

	if (source->actived && source->context.data){
		source->info.stop(source);
		os_event_signal(source->stopping_event);
		source->stop_code = 0;
	}
	source->actived = false;
}
