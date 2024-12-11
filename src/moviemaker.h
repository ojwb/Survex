//
//  moviemaker.h
//
//  Class for writing movies from Aven.
//
//  Copyright (C) 2004,2010,2011,2013,2014,2016,2023 Olly Betts
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//

#ifndef moviemaker_h
#define moviemaker_h

#ifndef PACKAGE
# error config.h must be included first in each C++ source file
#endif

#include <stdio.h>

struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
struct AVFrame;
struct AVPicture;
struct SwsContext;

#ifdef WITH_FFMPEG
extern "C" {
// To get LIBAVCODEC_VERSION_MAJOR defined:
# include <libavcodec/avcodec.h>
}
#endif

class MovieMaker {
#ifdef WITH_FFMPEG
    AVFormatContext *oc = nullptr;
    AVStream *video_st = nullptr;
# if LIBAVCODEC_VERSION_MAJOR < 57
#  ifndef HAVE_AVCODEC_ENCODE_VIDEO2
    int out_size = 0; // Legacy-only.
#  endif
# endif
    AVCodecContext *context = nullptr;
    AVFrame *frame = nullptr;
# if LIBAVCODEC_VERSION_MAJOR < 57
#  ifndef HAVE_AVCODEC_ENCODE_VIDEO2
    unsigned char *outbuf = nullptr; // Legacy-only.
#  endif
# endif
    unsigned char *pixels = nullptr;
    SwsContext *sws_ctx = nullptr;
    int averrno = 0;
    FILE* fh_to_close = nullptr;

    int encode_frame(AVFrame* frame);
    void release();
#endif

public:
    MovieMaker();
    bool Open(FILE* fh, const char* ext, int width, int height);
    unsigned char * GetBuffer() const;
    int GetWidth() const;
    int GetHeight() const;
    bool AddFrame();
    bool Close();
    ~MovieMaker();
    const char * get_error_string() const;
};

#endif
