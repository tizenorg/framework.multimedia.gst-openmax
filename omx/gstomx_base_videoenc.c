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

#include "gstomx_base_videoenc.h"
#include "gstomx.h"

#include <string.h>             /* for strcmp */

enum
{
  ARG_0,
  ARG_BITRATE,
};

#define DEFAULT_BITRATE 0

GSTOMX_BOILERPLATE (GstOmxBaseVideoEnc, gst_omx_base_videoenc, GstOmxBaseFilter,
    GST_OMX_BASE_FILTER_TYPE);

static void
process_output_buf(GstOmxBaseFilter * omx_base, GstBuffer * buf, OMX_BUFFERHEADERTYPE *omx_buffer)
{
  GstOmxBaseVideoEnc *self;

  self = GST_OMX_BASE_VIDEOENC (omx_base);

  GST_LOG_OBJECT (self, "base videoenc process_output_buf enter");

  /* MODIFICATION: Set sync frame info while encoding */
  if (omx_buffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME) {
    GST_BUFFER_FLAG_UNSET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }
}

static void
type_base_init (gpointer g_class)
{
}

static void
set_property (GObject * obj,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOmxBaseVideoEnc *self;

  self = GST_OMX_BASE_VIDEOENC (obj);

  switch (prop_id) {
    case ARG_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
  }
}

static void
get_property (GObject * obj, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstOmxBaseVideoEnc *self;

  self = GST_OMX_BASE_VIDEOENC (obj);

  switch (prop_id) {
    case ARG_BITRATE:
            /** @todo propagate this to OpenMAX when processing. */
      g_value_set_uint (value, self->bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
  }
}

static void
type_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstOmxBaseFilterClass *basefilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  basefilter_class = GST_OMX_BASE_FILTER_CLASS (g_class);

  /* Properties stuff */
  {
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    g_object_class_install_property (gobject_class, ARG_BITRATE,
        g_param_spec_uint ("bitrate", "Bit-rate",
            "Encoding bit-rate",
            0, G_MAXUINT, DEFAULT_BITRATE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }
  basefilter_class->process_output_buf = process_output_buf;
}

static gboolean
sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstOmxBaseVideoEnc *self;
  GstOmxBaseFilter *omx_base;
  GOmxCore *gomx;
  OMX_COLOR_FORMATTYPE color_format = OMX_COLOR_FormatUnused;
  gint width = 0;
  gint height = 0;
  const GValue *framerate = NULL;

  self = GST_OMX_BASE_VIDEOENC (GST_PAD_PARENT (pad));
  omx_base = GST_OMX_BASE_FILTER (self);
  gomx = (GOmxCore *) omx_base->gomx;

  GST_INFO_OBJECT (self, "setcaps (sink): %" GST_PTR_FORMAT, caps);

  g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
    guint32 fourcc;

    framerate = gst_structure_get_value (structure, "framerate");
    if (framerate) {
      self->framerate_num = gst_value_get_fraction_numerator (framerate);
      self->framerate_denom = gst_value_get_fraction_denominator (framerate);
    }

    if (gst_structure_get_fourcc (structure, "format", &fourcc)) {
      switch (fourcc) {
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        case GST_MAKE_FOURCC ('S', '4', '2', '0'):
          color_format = OMX_COLOR_FormatYUV420PackedPlanar;
          break;
        case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
          color_format = OMX_COLOR_FormatYCbYCr;
          break;
        case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
          color_format = OMX_COLOR_FormatCbYCrY;
          break;
        /* Add extended_color_format */
        case GST_MAKE_FOURCC ('S', 'T', '1', '2'):
          color_format = OMX_EXT_COLOR_FormatNV12TPhysicalAddress;
          break;
        case GST_MAKE_FOURCC ('S', 'N', '1', '2'):
          color_format = OMX_EXT_COLOR_FormatNV12LPhysicalAddress;
          break;
      }
    }
  }

  {
    OMX_PARAM_PORTDEFINITIONTYPE param;

    G_OMX_INIT_PARAM (param);

    /* Input port configuration. */
    {
      param.nPortIndex = omx_base->in_port->port_index;
      OMX_GetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);

      param.format.video.nFrameWidth = width;
      param.format.video.nFrameHeight = height;
      param.format.video.eColorFormat = color_format;
      if (framerate) {
        /* convert to Q.16 */
        param.format.video.xFramerate =
            (gst_value_get_fraction_numerator (framerate) << 16) /
            gst_value_get_fraction_denominator (framerate);
      }

      OMX_SetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);
    }

    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->out_port->port_index;
    OMX_GetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);

    param.nBufferSize = MAX(param.nBufferSize, width * height * 3 / 2);
    OMX_SetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);
  }

  return gst_pad_set_caps (pad, caps);
}

static void
omx_setup (GstOmxBaseFilter * omx_base)
{
  GstOmxBaseVideoEnc *self;
  GOmxCore *gomx;

  self = GST_OMX_BASE_VIDEOENC (omx_base);
  gomx = (GOmxCore *) omx_base->gomx;

  GST_INFO_OBJECT (omx_base, "begin");

  {
    OMX_PARAM_PORTDEFINITIONTYPE param;

    G_OMX_INIT_PARAM (param);

    /* Output port configuration. */
    {
      param.nPortIndex = omx_base->out_port->port_index;
      OMX_GetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);

      param.format.video.eCompressionFormat = self->compression_format;

      if (self->bitrate != 0)
        param.format.video.nBitrate = self->bitrate;

      OMX_SetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);
    }
  }

  GST_INFO_OBJECT (omx_base, "end");
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *omx_base;
  GstOmxBaseVideoEnc *self;

  omx_base = GST_OMX_BASE_FILTER (instance);
  self = GST_OMX_BASE_VIDEOENC (instance);

  omx_base->omx_setup = omx_setup;

  gst_pad_set_setcaps_function (omx_base->sinkpad, sink_setcaps);

  self->bitrate = DEFAULT_BITRATE;
}
