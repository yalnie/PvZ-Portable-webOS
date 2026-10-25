/*
 * Portions of this file are based on the PopCap Games Framework
 * Copyright (C) 2005-2009 PopCap Games, Inc.
 *
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later AND LicenseRef-PopCap
 *
 * This file is part of PvZ-Portable.
 *
 * PvZ-Portable is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PvZ-Portable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PvZ-Portable. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Compatibility shims for older webOS SDL2 libraries.
 *
 * Older webOS firmwares (webOS 1.x through 4.x) ship an older system libSDL2
 * which lacks several symbols introduced in later SDL 2.0.x releases:
 *   - SDL_GetHintBoolean (SDL 2.0.5)
 *   - SDL_CreateRGBSurfaceWithFormatFrom (SDL 2.0.5)
 *   - SDL_LoadFile_RW (SDL 2.0.6)
 *   - SDL_AudioStream API (SDL 2.0.7)
 *   - SDL_exp (SDL 2.0.8)
 *   - SDL_RWsize, SDL_RWseek, SDL_RWtell, SDL_RWread, SDL_RWwrite, SDL_RWclose (SDL 2.0.10)
 *   - SDL_strtokr (SDL 2.0.10)
 *   - SDL_AtomicSet, SDL_AtomicGet (older webOS firmwares had these as inline/macros)
 *
 * By providing and exporting these functions from the main binary, ELF dynamic
 * linking resolves them locally on old firmwares while remaining 100% compatible
 * with newer firmwares.
 */

#if defined(WEBOS) || defined(__WEBOS__)

#include <SDL.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <vector>
#include <mutex>
#include <algorithm>
#include <new>

// Undefine macros that might be defined by older/newer SDL headers
#undef SDL_RWsize
#undef SDL_RWseek
#undef SDL_RWtell
#undef SDL_RWread
#undef SDL_RWwrite
#undef SDL_RWclose
#undef SDL_LoadFile_RW
#undef SDL_GetHintBoolean
#undef SDL_AtomicSet
#undef SDL_AtomicGet
#undef SDL_exp
#undef SDL_strtokr
#undef SDL_CreateRGBSurfaceWithFormatFrom
#undef SDL_NewAudioStream
#undef SDL_FreeAudioStream
#undef SDL_AudioStreamPut
#undef SDL_AudioStreamGet
#undef SDL_AudioStreamAvailable
#undef SDL_AudioStreamClear
#undef SDL_AudioStreamFlush

#define WEBOS_COMPAT_EXPORT extern "C" __attribute__((visibility("default")))

// Implementation of SDL_AudioStream
struct _SDL_AudioStream {
    SDL_AudioCVT cvt;
    std::vector<uint8_t> buffer;
    size_t read_pos;
    std::mutex mtx;

    _SDL_AudioStream() : read_pos(0) {
        std::memset(&cvt, 0, sizeof(cvt));
    }
};

WEBOS_COMPAT_EXPORT SDL_bool SDL_GetHintBoolean(const char *name, SDL_bool default_value)
{
    const char *val = SDL_GetHint(name);
    if (!val || !*val) {
        return default_value;
    }
    if (*val == '0' || strcasecmp(val, "false") == 0 || strcasecmp(val, "off") == 0 || strcasecmp(val, "no") == 0) {
        return SDL_FALSE;
    }
    return SDL_TRUE;
}

WEBOS_COMPAT_EXPORT Sint64 SDL_RWsize(SDL_RWops *context)
{
    if (!context || !context->size) {
        return -1;
    }
    return context->size(context);
}

WEBOS_COMPAT_EXPORT Sint64 SDL_RWseek(SDL_RWops *context, Sint64 offset, int whence)
{
    if (!context || !context->seek) {
        return -1;
    }
    return context->seek(context, offset, whence);
}

WEBOS_COMPAT_EXPORT Sint64 SDL_RWtell(SDL_RWops *context)
{
    if (!context || !context->seek) {
        return -1;
    }
    return context->seek(context, 0, RW_SEEK_CUR);
}

WEBOS_COMPAT_EXPORT size_t SDL_RWread(SDL_RWops *context, void *ptr, size_t size, size_t maxnum)
{
    if (!context || !context->read) {
        return 0;
    }
    return context->read(context, ptr, size, maxnum);
}

WEBOS_COMPAT_EXPORT size_t SDL_RWwrite(SDL_RWops *context, const void *ptr, size_t size, size_t num)
{
    if (!context || !context->write) {
        return 0;
    }
    return context->write(context, ptr, size, num);
}

WEBOS_COMPAT_EXPORT int SDL_RWclose(SDL_RWops *context)
{
    if (!context || !context->close) {
        return 0;
    }
    return context->close(context);
}

WEBOS_COMPAT_EXPORT void *SDL_LoadFile_RW(SDL_RWops *src, size_t *datasize, int freesrc)
{
    if (!src) {
        if (datasize) *datasize = 0;
        return nullptr;
    }

    Sint64 size = (src->size) ? src->size(src) : -1;
    size_t alloc_size = (size > 0) ? static_cast<size_t>(size) : 1024;
    size_t total_read = 0;
    char *data = static_cast<char*>(SDL_malloc(alloc_size + 1));
    if (!data) {
        if (freesrc && src->close) src->close(src);
        if (datasize) *datasize = 0;
        return nullptr;
    }

    for (;;) {
        if (total_read >= alloc_size) {
            alloc_size = alloc_size * 2;
            char *newdata = static_cast<char*>(SDL_realloc(data, alloc_size + 1));
            if (!newdata) {
                SDL_free(data);
                if (freesrc && src->close) src->close(src);
                if (datasize) *datasize = 0;
                return nullptr;
            }
            data = newdata;
        }

        size_t n = (src->read) ? src->read(src, data + total_read, 1, alloc_size - total_read) : 0;
        if (n == 0) {
            break;
        }
        total_read += n;
    }

    data[total_read] = '\0';
    if (datasize) {
        *datasize = total_read;
    }
    if (freesrc && src->close) {
        src->close(src);
    }
    return data;
}

WEBOS_COMPAT_EXPORT double SDL_exp(double x)
{
    return std::exp(x);
}

WEBOS_COMPAT_EXPORT int SDL_AtomicSet(SDL_atomic_t *a, int v)
{
    return __atomic_exchange_n(&a->value, v, __ATOMIC_SEQ_CST);
}

WEBOS_COMPAT_EXPORT int SDL_AtomicGet(SDL_atomic_t *a)
{
    return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST);
}

WEBOS_COMPAT_EXPORT char *SDL_strtokr(char *s1, const char *s2, char **saveptr)
{
    return strtok_r(s1, s2, saveptr);
}

WEBOS_COMPAT_EXPORT SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels,
                                                                   int width,
                                                                   int height,
                                                                   int depth,
                                                                   int pitch,
                                                                   Uint32 format)
{
    Uint32 Rmask = 0, Gmask = 0, Bmask = 0, Amask = 0;
    int bpp = depth;
    if (SDL_ISPIXELFORMAT_INDEXED(format)) {
        bpp = SDL_BITSPERPIXEL(format);
    } else {
        if (!SDL_PixelFormatEnumToMasks(format, &bpp, &Rmask, &Gmask, &Bmask, &Amask)) {
            return nullptr;
        }
    }
    return SDL_CreateRGBSurfaceFrom(pixels, width, height, bpp, pitch, Rmask, Gmask, Bmask, Amask);
}

WEBOS_COMPAT_EXPORT SDL_AudioStream *SDL_NewAudioStream(const SDL_AudioFormat src_format,
                                                        const Uint8 src_channels,
                                                        const int src_rate,
                                                        const SDL_AudioFormat dst_format,
                                                        const Uint8 dst_channels,
                                                        const int dst_rate)
{
    auto *stream = new (std::nothrow) _SDL_AudioStream();
    if (!stream) {
        return nullptr;
    }
    if (src_format != dst_format || src_channels != dst_channels || src_rate != dst_rate) {
        if (SDL_BuildAudioCVT(&stream->cvt, src_format, src_channels, src_rate,
                              dst_format, dst_channels, dst_rate) < 0) {
            delete stream;
            return nullptr;
        }
    }
    return stream;
}

WEBOS_COMPAT_EXPORT int SDL_AudioStreamPut(SDL_AudioStream *stream, const void *buf, int len)
{
    if (!stream || !buf || len <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(stream->mtx);
    if (stream->cvt.needed) {
        int workbuflen = len * stream->cvt.len_mult;
        std::vector<uint8_t> workbuf(workbuflen);
        std::memcpy(workbuf.data(), buf, len);
        stream->cvt.buf = workbuf.data();
        stream->cvt.len = len;
        if (SDL_ConvertAudio(&stream->cvt) < 0) {
            return -1;
        }
        int outlen = stream->cvt.len_cvt;
        stream->buffer.insert(stream->buffer.end(), workbuf.data(), workbuf.data() + outlen);
    } else {
        const uint8_t *p = static_cast<const uint8_t*>(buf);
        stream->buffer.insert(stream->buffer.end(), p, p + len);
    }
    return 0;
}

WEBOS_COMPAT_EXPORT int SDL_AudioStreamGet(SDL_AudioStream *stream, void *buf, int len)
{
    if (!stream || !buf || len <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(stream->mtx);
    if (stream->read_pos >= stream->buffer.size()) {
        return 0;
    }
    size_t available = stream->buffer.size() - stream->read_pos;
    size_t to_copy = std::min(available, static_cast<size_t>(len));
    std::memcpy(buf, stream->buffer.data() + stream->read_pos, to_copy);
    stream->read_pos += to_copy;
    if (stream->read_pos >= stream->buffer.size()) {
        stream->buffer.clear();
        stream->read_pos = 0;
    } else if (stream->read_pos > 32768) {
        stream->buffer.erase(stream->buffer.begin(), stream->buffer.begin() + stream->read_pos);
        stream->read_pos = 0;
    }
    return static_cast<int>(to_copy);
}

WEBOS_COMPAT_EXPORT int SDL_AudioStreamAvailable(SDL_AudioStream *stream)
{
    if (!stream) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(stream->mtx);
    if (stream->read_pos >= stream->buffer.size()) {
        return 0;
    }
    return static_cast<int>(stream->buffer.size() - stream->read_pos);
}

WEBOS_COMPAT_EXPORT void SDL_AudioStreamClear(SDL_AudioStream *stream)
{
    if (!stream) {
        return;
    }
    std::lock_guard<std::mutex> lock(stream->mtx);
    stream->buffer.clear();
    stream->read_pos = 0;
}

WEBOS_COMPAT_EXPORT int SDL_AudioStreamFlush(SDL_AudioStream *stream)
{
    (void)stream;
    return 0;
}

WEBOS_COMPAT_EXPORT void SDL_FreeAudioStream(SDL_AudioStream *stream)
{
    delete stream;
}

#endif // defined(WEBOS) || defined(__WEBOS__)
