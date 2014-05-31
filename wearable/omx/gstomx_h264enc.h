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

#ifndef GSTOMX_H264ENC_H
#define GSTOMX_H264ENC_H

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_OMX_H264ENC(obj) (GstOmxH264Enc *) (obj)
#define GST_OMX_H264ENC_TYPE (gst_omx_h264enc_get_type ())
typedef struct GstOmxH264Enc GstOmxH264Enc;
typedef struct GstOmxH264EncClass GstOmxH264EncClass;

typedef struct PrependSPSPPSToIDRFramesParams PrependSPSPPSToIDRFramesParams;
typedef struct PhysicalOutputParams PhysicalOutputParams;

#include "gstomx_base_videoenc.h"
#include "gstomx_h264.h"

struct GstOmxH264Enc
{
  GstOmxBaseVideoEnc omx_base;
  gboolean byte_stream;

  GstBuffer *dci;
  gboolean append_dci;
  gboolean first_frame;

  OMX_VIDEO_PARAM_AVCTYPE h264type;
  OMX_VIDEO_PARAM_AVCSLICEFMO slice_fmo;
};

struct GstOmxH264EncClass
{
  GstOmxBaseVideoEncClass parent_class;
};

struct PrependSPSPPSToIDRFramesParams {
  OMX_U32 nSize;
  OMX_VERSIONTYPE nVersion;
  OMX_BOOL bEnable;
};

struct PhysicalOutputParams {
  OMX_U32 nSize;
  OMX_VERSIONTYPE nVersion;
  OMX_BOOL bEnable;
};

GType gst_omx_h264enc_get_type (void);

G_END_DECLS
#endif /* GSTOMX_H264ENC_H */
