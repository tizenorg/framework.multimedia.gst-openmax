/*
 * Copyright (C) 2007-2009 Nokia Corporation.
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef GSTOMX_MP3DEC_ALP_H
#define GSTOMX_MP3DEC_ALP_H

#include <gst/gst.h>
#include <iniparser.h>

G_BEGIN_DECLS
#define GST_OMX_MP3DEC_ALP(obj) (GstOmxMp3DecAlp *) (obj)
#define GST_OMX_MP3DEC_ALP_TYPE (gst_omx_mp3dec_alp_get_type ())
typedef struct GstOmxMp3DecAlp GstOmxMp3DecAlp;
typedef struct GstOmxMp3DecAlpClass GstOmxMp3DecAlpClass;

#include "gstomx_base_audiodec.h"

#define SRP_ENABLE_DUMP
#define SRP_DUMP_INI_DEFAULT_PATH   "/usr/etc/mmfw_audio_pcm_dump.ini"
#define SRP_DUMP_INI_TEMP_PATH      "/opt/system/mmfw_audio_pcm_dump.ini"
#define SRP_DUMP_INPUT_PATH_PREFIX  "/tmp/dump_codec_"

struct GstOmxMp3DecAlp
{
  GstOmxBaseAudioDec omx_base;
  GstBuffer *remain_buffer;
#ifdef SRP_ENABLE_DUMP
  /* File Descriptor to dump the pcm data */
  FILE *pcmFd;
#endif
};

struct GstOmxMp3DecAlpClass
{
  GstOmxBaseAudioDecClass parent_class;
};

GType gst_omx_mp3dec_alp_get_type (void);

G_END_DECLS
#endif /* GSTOMX_MP3DEC_ALP_H */
