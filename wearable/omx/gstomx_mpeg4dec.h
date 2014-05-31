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

#ifndef GSTOMX_MPEG4DEC_H
#define GSTOMX_MPEG4DEC_H

#include <gst/gst.h>
#include <stdint.h>
#include <dlfcn.h>
#include <stdlib.h>

G_BEGIN_DECLS
#define GST_OMX_MPEG4DEC(obj) (GstOmxMpeg4Dec *) (obj)
#define GST_OMX_MPEG4DEC_TYPE (gst_omx_mpeg4dec_get_type ())
#ifdef DIVX_DRM
typedef struct DivXSymbolTable DivXSymbolTable;
#endif
typedef struct GstOmxMpeg4Dec GstOmxMpeg4Dec;
typedef struct GstOmxMpeg4DecClass GstOmxMpeg4DecClass;

#include "gstomx_base_videodec.h"

#ifdef DIVX_DRM /* we do not use it now */
#define DIVX_SDK_PLUGIN_NAME "libmm_divxsdk.so"

typedef enum drmErrorCodes
{
  DRM_SUCCESS = 0,
  DRM_NOT_AUTHORIZED,
  DRM_NOT_REGISTERED,
  DRM_RENTAL_EXPIRED,
  DRM_GENERAL_ERROR,
  DRM_NEVER_REGISTERED,
} drmErrorCodes_t;

struct DivXSymbolTable
{
  uint8_t* (*init_decrypt) (int*);
  int (*commit) (uint8_t *);
  int (*decrypt_video) (uint8_t *, uint8_t *, uint32_t);
  int (*prepare_video_bitstream) (uint8_t *, uint8_t * , uint32_t ,  uint8_t * , uint32_t * );
  int (*finalize) (uint8_t *);
};
#endif

struct GstOmxMpeg4Dec
{
  GstOmxBaseVideoDec omx_base;
#ifdef DIVX_DRM
  uint8_t* drmContext;
  void *divx_handle;
  DivXSymbolTable divx_sym_table;
#endif
};

struct GstOmxMpeg4DecClass
{
  GstOmxBaseVideoDecClass parent_class;
};

GType gst_omx_mpeg4dec_get_type (void);

G_END_DECLS
#endif /* GSTOMX_MPEG4DEC_H */
