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
  ARG_FORCE_KEY_FRAME,
  ARG_IDR_PERIOD,
  ARG_SKIP_INBUF_COUNT,
  ARG_USE_STATETUNING, /* STATE_TUNING */
};

enum PortIndexType
{
  PORT_INDEX_IN = 0,
  PORT_INDEX_OUT = 1,
  PORT_INDEX_BOTH = -1,
  PORT_INDEX_NONE = -2
};

#define DEFAULT_BITRATE 0
#define DEFAULT_IDR_PERIOD 20
#define DEFAULT_SKIP_BY_FORCE_IFRAME 0

GSTOMX_BOILERPLATE (GstOmxBaseVideoEnc, gst_omx_base_videoenc, GstOmxBaseFilter,
    GST_OMX_BASE_FILTER_TYPE);

static void instance_init (GstElement * element);


/* modification: user force I frame */
static void
add_force_key_frame(GstOmxBaseVideoEnc *enc)
{
  GstOmxBaseFilter *omx_base;
  GOmxCore *gomx;
  OMX_CONFIG_INTRAREFRESHVOPTYPE config;
  OMX_ERRORTYPE ret = OMX_ErrorNone;

  omx_base = GST_OMX_BASE_FILTER (enc);
  gomx = (GOmxCore *) omx_base->gomx;

  GST_LOG_OBJECT (enc, "request forced key frame now.");

  if (!omx_base->out_port || !gomx->omx_handle) {
    GST_ERROR_OBJECT (enc, "failed to set force-i-frame...");
    return;
  }

  G_OMX_INIT_PARAM (config);
  config.nPortIndex = omx_base->out_port->port_index;
  config.IntraRefreshVOP = OMX_TRUE;

  ret = OMX_SetConfig (gomx->omx_handle, OMX_IndexConfigVideoIntraVOPRefresh, &config);
  if (ret == OMX_ErrorNone)
    GST_WARNING_OBJECT (enc, "forced key frame is done. (OMX_IndexConfigVideoIntraVOPRefresh) ret = %d", ret);
  else
    GST_ERROR_OBJECT (enc, "Failed to set forced key frame (OMX_IndexConfigVideoIntraVOPRefresh) ret = %d", ret);
}


static GstOmxReturn
process_input_buf (GstOmxBaseFilter * omx_base_filter, GstBuffer **buf)
{
  GstOmxBaseVideoEnc *self;

  self = GST_OMX_BASE_VIDEOENC (omx_base_filter);

  GST_LOG_OBJECT (self, "base videoenc process_input_buf enter");

  if (self->use_force_key_frame) { /* skip frame by cnt and make force key frame */
    if (self->skip_inbuf_cnt > 0) {
      GST_WARNING_OBJECT (self, "skip inbuf before enc force key frame (%d)", self->skip_inbuf_cnt);
      self->skip_inbuf_cnt--;
      return GSTOMX_RETURN_SKIP;

    } else if (self->skip_inbuf_cnt == 0) {
        add_force_key_frame (self);
    }

    self->use_force_key_frame = FALSE;
  }

  return GSTOMX_RETURN_OK;
}

/* modification: postprocess for outputbuf. in this videoenc case, set sync frame */
static GstOmxReturn
process_output_buf(GstOmxBaseFilter * omx_base, GstBuffer **buf, OMX_BUFFERHEADERTYPE *omx_buffer)
{
  GstOmxBaseVideoEnc *self;

  self = GST_OMX_BASE_VIDEOENC (omx_base);

  GST_LOG_OBJECT (self, "base videoenc process_output_buf enter");

  /* modification: set sync frame info while encoding */
  if (omx_buffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME) {
    GST_BUFFER_FLAG_UNSET(*buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET(*buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  return GSTOMX_RETURN_OK;
}

/* modification: get codec_data from omx component and set it caps */
static void
process_output_caps(GstOmxBaseFilter * self, OMX_BUFFERHEADERTYPE *omx_buffer)
{
  GstBuffer *buf;
  GstCaps *caps = NULL;
  GstStructure *structure;
  GValue value = { 0, {{0}
      }
  };

  caps = gst_pad_get_negotiated_caps (self->srcpad);
  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_new_and_alloc (omx_buffer->nFilledLen);
  memcpy (GST_BUFFER_DATA (buf),
      omx_buffer->pBuffer + omx_buffer->nOffset, omx_buffer->nFilledLen);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_structure_set_value (structure, "codec_data", &value);
  g_value_unset (&value);

  gst_pad_set_caps (self->srcpad, caps);
  gst_caps_unref (caps);
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
  GstOmxBaseFilter *omx_base;

  self = GST_OMX_BASE_VIDEOENC (obj);
  omx_base = GST_OMX_BASE_FILTER (obj);

  switch (prop_id) {
    case ARG_BITRATE:
    {
      self->bitrate = g_value_get_uint (value);
      /*Set the bitrate by using set config For Dynamic bitrate */
      if(omx_base->gomx->omx_state == OMX_StateExecuting) {
        OMX_VIDEO_CONFIG_BITRATETYPE config;
        OMX_ERRORTYPE ret = OMX_ErrorNone;
        G_OMX_INIT_PARAM (config);

        config.nPortIndex = omx_base->out_port->port_index;
        OMX_GetConfig (omx_base->gomx->omx_handle, OMX_IndexConfigVideoBitrate, &config);

        GST_WARNING_OBJECT (self, "bitrate was changed from %d to %d", config.nEncodeBitrate, self->bitrate);
        config.nEncodeBitrate = self->bitrate;
        ret = OMX_SetConfig (omx_base->gomx->omx_handle, OMX_IndexConfigVideoBitrate, &config);
        if (ret == OMX_ErrorNone)
          GST_WARNING_OBJECT (self, "set OMX_IndexConfigVideoBitrate for nEncodeBitrate = %d", self->bitrate);
        else
          GST_ERROR_OBJECT (self, "Failed to set OMX_IndexConfigVideoBitrate. nEncodeBitrate = %d  ret = %d", self->bitrate, ret);

      }
    }
      break;

    /* modification: request to component to make key frame */
    case ARG_FORCE_KEY_FRAME:
      self->use_force_key_frame = g_value_get_boolean (value);
      GST_WARNING_OBJECT (self, "set use_force_key_frame");
      break;

    case ARG_SKIP_INBUF_COUNT:
      self->skip_inbuf_cnt = g_value_get_int (value);
      GST_INFO_OBJECT (self, "set use_force_key_frame after %d frame skip", self->skip_inbuf_cnt);
      break;

    case ARG_IDR_PERIOD:
      self->idr_period = g_value_get_int (value);
      {
        OMX_VIDEO_PARAM_AVCTYPE param;
        OMX_ERRORTYPE ret = OMX_ErrorNone;

        G_OMX_INIT_PARAM (param);
        param.nPortIndex = omx_base->out_port->port_index;
        OMX_GetParameter (omx_base->gomx->omx_handle, OMX_IndexParamVideoAvc, &param);

        param.nPFrames = self->idr_period;
        ret = OMX_SetParameter (omx_base->gomx->omx_handle, OMX_IndexParamVideoAvc, &param);
        if (ret == OMX_ErrorNone)
          GST_WARNING_OBJECT (self, "set OMX_IndexParamVideoAvc for IDR period = %d", self->idr_period);
        else
          GST_ERROR_OBJECT (self, "Failed to set OMX_IndexParamVideoAvc. IDR period = %d  ret = %d", self->idr_period, ret);
      }
      break;

    case ARG_USE_STATETUNING:
      self->omx_base.use_state_tuning = g_value_get_boolean(value);
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
    case ARG_IDR_PERIOD:
      g_value_set_int (value, self->idr_period);
      break;
    case ARG_SKIP_INBUF_COUNT:
      g_value_set_int (value, self->skip_inbuf_cnt);
      break;
    case ARG_USE_STATETUNING:
      g_value_set_boolean(value, self->omx_base.use_state_tuning);
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

  GST_WARNING("video enc  type_class_init");
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

    g_object_class_install_property (gobject_class, ARG_FORCE_KEY_FRAME,
        g_param_spec_boolean ("force-i-frame", "force the encoder to produce I frame",
            "force the encoder to produce I frame",
            FALSE,
            G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, ARG_IDR_PERIOD,
        g_param_spec_int ("idr-period", "set interval of I-frame",
            "set interval of I-frame",
            0, G_MAXINT, DEFAULT_IDR_PERIOD,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, ARG_SKIP_INBUF_COUNT,
        g_param_spec_int ("skip-inbuf", "skip inbuf in case of force I frame",
            "skip inbuf in case of force I frame",
            0, G_MAXINT, DEFAULT_SKIP_BY_FORCE_IFRAME,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /* STATE_TUNING */
    g_object_class_install_property (gobject_class, ARG_USE_STATETUNING,
        g_param_spec_boolean ("state-tuning", "start omx component in gst paused state",
        "Whether or not to use state-tuning feature",
        FALSE, G_PARAM_READWRITE));
  }
  basefilter_class->process_input_buf = process_input_buf;
  basefilter_class->process_output_buf = process_output_buf;
  basefilter_class->process_output_caps = process_output_caps;
  basefilter_class->instance_init = instance_init;
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

  GST_WARNING_OBJECT (self, "setcaps (sink): %" GST_PTR_FORMAT, caps);

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
        /* MODIFICATION: Add extended_color_format */
        case GST_MAKE_FOURCC ('S', 'T', '1', '2'):
          color_format = OMX_EXT_COLOR_FormatNV12T_Phyaddr_Fd;
          break;
        case GST_MAKE_FOURCC ('S', 'N', '1', '2'):
          color_format = OMX_EXT_COLOR_FormatNV12L_Phyaddr_Fd;
          break;

      }
    }
  }

  if (gomx == NULL)
  {
    GST_ERROR_OBJECT(self, "GOmxCore is NULL! this can make seg fault");
  }

  if (gomx->omx_handle == NULL)
  {
    GST_ERROR_OBJECT(self, "omx_handle is NULL!  this can make seg fault");
  }

  /* Input port configuration. */
  {
    OMX_PARAM_PORTDEFINITIONTYPE param;
    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->in_port->port_index;
    OMX_GetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);

    param.format.video.nFrameWidth = width;
    param.format.video.nFrameHeight = height;
    param.format.video.eColorFormat = color_format;
    if (framerate) {
      /* convert to Q.16 */
      param.format.video.xFramerate =
          (OMX_U32)(gst_value_get_fraction_numerator (framerate) << 16) /
          (OMX_U32)gst_value_get_fraction_denominator (framerate);
    }

    OMX_SetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);
  }

  /* Output port configuration. */
  {
    OMX_PARAM_PORTDEFINITIONTYPE param;
    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->out_port->port_index;
    OMX_GetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);

    param.nBufferSize = width * height * 3 / 2;

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

      if (self->bitrate > 0)
        param.format.video.nBitrate = self->bitrate;

      OMX_SetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);
    }
  }

  /* modification: set bitrate by using OMX_IndexParamVideoBitrate macro*/
  if (self->bitrate > 0) {
    if (gomx->component_vendor == GOMX_VENDOR_SLSI_EXYNOS) {

      /* set Bitrate and control rate*/
      {
        OMX_VIDEO_PARAM_BITRATETYPE param_bitrate;
        G_OMX_INIT_PARAM (param_bitrate);
        param_bitrate.nPortIndex = omx_base->out_port->port_index;
        OMX_GetParameter (gomx->omx_handle, OMX_IndexParamVideoBitrate, &param_bitrate);

        param_bitrate.nTargetBitrate = self->bitrate;

        param_bitrate.eControlRate = OMX_Video_ControlRateVariable; /* VBR */
        GST_WARNING_OBJECT (self, "set bitrate (OMX_Video_ControlRateVariable): %d", param_bitrate.nTargetBitrate);
        OMX_SetParameter (gomx->omx_handle, OMX_IndexParamVideoBitrate, &param_bitrate);
      }

      /* set Quantization parameter*/
      {
        OMX_VIDEO_PARAM_QUANTIZATIONTYPE param_qp;
        G_OMX_INIT_PARAM (param_qp);
        param_qp.nPortIndex = omx_base->out_port->port_index;
        OMX_GetParameter (gomx->omx_handle, OMX_IndexParamVideoQuantization, &param_qp);

        param_qp.nQpI = 20;
        param_qp.nQpP = 20;
        param_qp.nQpB = 20;

        GST_WARNING_OBJECT (self, "set quantization parameter (nQpI: %d  nQpP: %d  nQpB: %d)", param_qp.nQpI, param_qp.nQpP, param_qp.nQpB);
        OMX_SetParameter (gomx->omx_handle, OMX_IndexParamVideoQuantization, &param_qp);
      }

    } else {
      OMX_VIDEO_PARAM_BITRATETYPE param;
      G_OMX_INIT_PARAM (param);
      param.nPortIndex = omx_base->out_port->port_index;
      OMX_GetParameter (gomx->omx_handle, OMX_IndexParamVideoBitrate, &param);

      param.nTargetBitrate = self->bitrate;

      param.eControlRate = OMX_Video_ControlRateVariable; /* VBR */
      GST_WARNING_OBJECT (self, "set bitrate (OMX_Video_ControlRateVariable): %d", param.nTargetBitrate);
      OMX_SetParameter (gomx->omx_handle, OMX_IndexParamVideoBitrate, &param);
    }
  }

  GST_INFO_OBJECT (omx_base, "end");
}

static void
instance_private_value_init(GstElement * element)
{
  GstOmxBaseFilter *omx_base;
  GstOmxBaseVideoEnc *self;

  omx_base = GST_OMX_BASE_FILTER (element);
  self = GST_OMX_BASE_VIDEOENC (element);

  self->bitrate = DEFAULT_BITRATE;
  self->use_force_key_frame = FALSE;
  self->skip_inbuf_cnt = DEFAULT_SKIP_BY_FORCE_IFRAME;

  omx_base->gomx->codec_type = GSTOMX_CODECTYPE_VIDEO_ENC;
}

static void
instance_init (GstElement * element)
{
  GST_OMX_BASE_FILTER_CLASS(parent_class)->instance_init(element);

  instance_private_value_init(element);
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *omx_base;

  GST_WARNING("begin");
  omx_base = GST_OMX_BASE_FILTER (instance);

  instance_private_value_init(GST_ELEMENT(instance));

  omx_base->omx_setup = omx_setup;

  gst_pad_set_setcaps_function (omx_base->sinkpad, sink_setcaps);

  GST_WARNING("end");
}
