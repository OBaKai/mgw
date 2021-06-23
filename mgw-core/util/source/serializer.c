#include "serializer.h"

size_t s_read(struct serializer *s, void *data, size_t size)
{
	if (s && s->read && data && size)
		return s->read(s->data, (void*)data, size);
	return 0;
}

size_t s_write(struct serializer *s, const void *data,
		size_t size)
{
	if (s && s->write && data && size)
		return s->write(s->data, (void*)data, size);
	return 0;
}

size_t serialize(struct serializer *s, void *data, size_t len)
{
	if (s) {
		if (s->write)
			return s->write(s->data, data, len);
		else if (s->read)
			return s->read(s->data, data, len);
	}

	return 0;
}

int64_t serializer_seek(struct serializer *s, int64_t offset,
		enum serialize_seek_type seek_type)
{
	if (s && s->seek)
		return s->seek(s->data, offset, seek_type);
	return -1;
}

int64_t serializer_get_pos(struct serializer *s)
{
	if (s && s->get_pos)
		return s->get_pos(s->data);
	return -1;
}

/* formatted this to be similar to the AVIO layout that ffmpeg uses */

void s_w8(struct serializer *s, uint8_t u8)
{
	s_write(s, &u8, sizeof(uint8_t));
}

void s_wl16(struct serializer *s, uint16_t u16)
{
	s_w8(s, (uint8_t)u16);
	s_w8(s, u16 >> 8);
}

void s_wl24(struct serializer *s, uint32_t u24)
{
	s_w8  (s, (uint8_t)u24);
	s_wl16(s, (uint16_t)(u24 >> 8));
}

void s_wl32(struct serializer *s, uint32_t u32)
{
	s_wl16(s, (uint16_t)u32);
	s_wl16(s, (uint16_t)(u32 >> 16));
}

void s_wl64(struct serializer *s, uint64_t u64)
{
	s_wl32(s, (uint32_t)u64);
	s_wl32(s, (uint32_t)(u64 >> 32));
}

void s_wlf(struct serializer *s, float f)
{
	s_wl32(s, *(uint32_t*)&f);
}

void s_wld(struct serializer *s, double d)
{
	s_wl64(s, *(uint64_t*)&d);
}

void s_wb16(struct serializer *s, uint16_t u16)
{
	s_w8(s, u16 >> 8);
	s_w8(s, (uint8_t)u16);
}

void s_wb24(struct serializer *s, uint32_t u24)
{
	s_wb16(s, (uint16_t)(u24 >> 8));
	s_w8  (s, (uint8_t)u24);
}

void s_wb32(struct serializer *s, uint32_t u32)
{
	s_wb16(s, (uint16_t)(u32 >> 16));
	s_wb16(s, (uint16_t)u32);
}

void s_wb64(struct serializer *s, uint64_t u64)
{
	s_wb32(s, (uint32_t)(u64 >> 32));
	s_wb32(s, (uint32_t)u64);
}

void s_wbf(struct serializer *s, float f)
{
	s_wb32(s, *(uint32_t*)&f);
}

void s_wbd(struct serializer *s, double d)
{
	s_wb64(s, *(uint64_t*)&d);
}
