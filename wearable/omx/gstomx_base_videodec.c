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

#include "gstomx_base_videodec.h"
#include "gstomx.h"

enum
{
  ARG_0,
  ARG_USE_STATETUNING, /* STATE_TUNING */
};

GSTOMX_BOILERPLATE (GstOmxBaseVideoDec, gst_omx_base_videodec, GstOmxBaseFilter,
    GST_OMX_BASE_FILTER_TYPE);


static void instance_init (GstElement * element);


static GstOmxReturn
process_input_buf (GstOmxBaseFilter * omx_base_filter, GstBuffer **buf)
{
  return GSTOMX_RETURN_OK;
}

static void
set_enable_platformSpecificBuffer(GstOmxBaseFilter * self)
{
  OMX_INDEXTYPE index = OMX_IndexComponentStartUnused;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  EnableGemBuffersParams params;
  GOmxCore *gomx = self->gomx;

  GST_LOG_OBJECT(self, "set_enable_platformSpecificBuffer enter");

  err = OMX_GetExtensionIndex(gomx->omx_handle, "OMX.SEC.index.enablePlatformSpecificBuffers", &index);
  if (err != OMX_ErrorNone || index == OMX_IndexComponentStartUnused) {
      GST_INFO_OBJECT(self, "can not get index for OMX_GetExtensionIndex enablePlatformSpecificBuffers");
      return;
  }

  G_OMX_INIT_PARAM(params);

  params.nPortIndex = self->out_port->port_index;
  params.enable = OMX_TRUE;

  err = OMX_SetParameter(gomx->omx_handle, index, &params);
  if (err == OMX_ErrorNone) {
    GST_INFO_OBJECT(self, "set_enable_platformSpecificBuffer success.");
  } else {
    GST_ERROR_OBJECT(self, "set OMX_IndexParamEnablePlatformSpecificBuffers failed with error %d (0x%08x)", err, err);
  }

  return;
}

static void
type_base_init (gpointer g_class)
{
}

/* MODIFICATION: add state tuning property */
static void
set_property (GObject * obj,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOmxBaseVideoDec *self;

  self = GST_OMX_BASE_VIDEODEC (obj);

  switch (prop_id) {
    /* STATE_TUNING */
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
  GstOmxBaseVideoDec *self;

  self = GST_OMX_BASE_VIDEODEC (obj);

  switch (prop_id) {
    /* STATE_TUNING */
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

  gobject_class = G_OBJECT_CLASS (g_class);
  basefilter_class = GST_OMX_BASE_FILTER_CLASS (g_class);
  GST_DEBUG("videodec  type_class_init");

  /* Properties stuff */
  {
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    /* STATE_TUNING */
    g_object_class_install_property (gobject_class, ARG_USE_STATETUNING,
        g_param_spec_boolean ("state-tuning", "start omx component in gst paused state",
        "Whether or not to use state-tuning feature",
        FALSE, G_PARAM_READWRITE));
  }
  basefilter_class->process_input_buf = process_input_buf;
  basefilter_class->instance_init = instance_init;
}

static void
settings_changed_cb (GOmxCore * core)
{
  GstOmxBaseFilter *omx_base;
  GstOmxBaseVideoDec *self;
  guint width;
  guint height;
  guint32 format = 0;

  OMX_PARAM_PORTDEFINITIONTYPE param;

  GstCaps *new_caps;
  GstStructure *struc;

  omx_base = core->object;
  self = GST_OMX_BASE_VIDEODEC (omx_base);

  GST_DEBUG_OBJECT (omx_base, "settings changed");

  G_OMX_INIT_PARAM (param);

  param.nPortIndex = omx_base->out_port->port_index;
  OMX_GetParameter (omx_base->gomx->omx_handle, OMX_IndexParamPortDefinition,
    &param);

  width = param.format.video.nFrameWidth;
  height = param.format.video.nFrameHeight;
  GST_INFO_OBJECT (omx_base, "Port settings changed: nFrameWidth = %d  nFrameHeight = %d", width, height);

  /* Modification: h264 can has crop. modify for trim */
  if ((self->compression_format == OMX_VIDEO_CodingAVC) &&
    (core->crop_changed == TRUE)) {
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_CONFIG_RECTTYPE config;

    G_OMX_INIT_PARAM (config);
    config.nPortIndex = omx_base->out_port->port_index;
    ret = OMX_GetConfig (omx_base->gomx->omx_handle, OMX_IndexConfigCommonOutputCrop, &config);

    if (ret == OMX_ErrorNone) {
      GST_INFO_OBJECT (omx_base, "Port settings changed: CROP: nLeft = %d, nTop = %d, nWidth = %d, nHeight = %d",
           config.nLeft, config.nTop, config.nWidth, config.nHeight);
      width = config.nWidth;
      height = config.nHeight;
    }
    core->crop_changed = FALSE;
  }

  GST_LOG_OBJECT (omx_base, "settings changed: fourcc =0x%x", (guint)param.format.video.eColorFormat);
  switch ((guint)param.format.video.eColorFormat) {
    case OMX_COLOR_FormatYUV420Planar:
    case OMX_COLOR_FormatYUV420PackedPlanar:
      format = GST_MAKE_FOURCC ('I', '4', '2', '0');
      break;
    case OMX_COLOR_FormatYCbYCr:
      format = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
    case OMX_COLOR_FormatCbYCrY:
      format = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
      break;
    /* MODIFICATION: Add extended_color_format */
    case OMX_EXT_COLOR_FormatNV12T_Phyaddr_Fd:
      format = GST_MAKE_FOURCC ('S', 'T', '1', '2');
      break;
    case OMX_EXT_COLOR_FormatNV12L_Phyaddr_Fd:
      format = GST_MAKE_FOURCC ('S', 'N', '1', '2');
      break;
    case OMX_COLOR_FormatYUV420SemiPlanar:
      format = GST_MAKE_FOURCC ('N', 'V', '1', '2');
      break;
    default:
      break;
  }

  new_caps = gst_caps_new_empty ();
  struc = gst_structure_new ("video/x-raw-yuv",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height, "format", GST_TYPE_FOURCC, format, NULL);

  if (self->framerate_denom != 0)
    gst_structure_set (struc, "framerate", GST_TYPE_FRACTION,
        self->framerate_num, self->framerate_denom, NULL);
  else
    /* FIXME this is a workaround for xvimagesink */
    gst_structure_set (struc, "framerate", GST_TYPE_FRACTION, 0, 1, NULL);

  gst_caps_append_structure (new_caps, struc);

  GST_WARNING_OBJECT (omx_base, "caps are: %" GST_PTR_FORMAT, new_caps);
  gst_pad_set_caps (omx_base->srcpad, new_caps);

  gst_caps_unref (new_caps);
}

static gboolean
sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstOmxBaseVideoDec *self;
  GstOmxBaseFilter *omx_base;
  GOmxCore *gomx;
  OMX_PARAM_PORTDEFINITIONTYPE param;
  gint width = 0;
  gint height = 0;
  gboolean hls_streaming = FALSE;
  const char *stream_type;

  self = GST_OMX_BASE_VIDEODEC (GST_PAD_PARENT (pad));
  omx_base = GST_OMX_BASE_FILTER (self);

  gomx = (GOmxCore *) omx_base->gomx;

  GST_WARNING_OBJECT (self, "setcaps (sink): %" GST_PTR_FORMAT, caps);

  g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  stream_type = gst_structure_get_name(structure);

    if (g_str_has_prefix(stream_type, "video/x-divx") || g_str_has_prefix(stream_type, "video/x-xvid"))
    {
        self->IsDivx=TRUE;
        GST_INFO_OBJECT (self, "this content is divx so we need to set the IsDivx");
    }
    else
        self->IsDivx=FALSE;

  {
    const GValue *framerate = NULL;
    framerate = gst_structure_get_value (structure, "framerate");
    if (framerate) {
      self->framerate_num = gst_value_get_fraction_numerator (framerate);
      self->framerate_denom = gst_value_get_fraction_denominator (framerate);
    }
    omx_base->duration = gst_util_uint64_scale_int (GST_SECOND, self->framerate_denom, self->framerate_num);
    GST_INFO_OBJECT (self, "set average duration= %"GST_TIME_FORMAT, GST_TIME_ARGS (omx_base->duration));
  }

  G_OMX_INIT_PARAM (param);

  {
    const GValue *codec_data;
    GstBuffer *buffer;

    codec_data = gst_structure_get_value (structure, "codec_data");
    if (codec_data) {
      buffer = gst_value_get_buffer (codec_data);
      omx_base->codec_data = buffer;
      gst_buffer_ref (buffer);
    }
  }

  if (gomx == NULL && omx_base->use_state_tuning == FALSE)
  {
    GST_ERROR_OBJECT(self, "GOmxCore is NULL! sink_setcaps return FALSE");
    return FALSE; /* we can do this only for B2.  */
  }

  if (gomx->omx_handle == NULL && omx_base->use_state_tuning == FALSE)
  {
    GST_ERROR_OBJECT(self, "omx_handle is NULL! sink_setcaps return FALSE");
    return FALSE; /* we can do this only for B2 */
  }

  if ((width <= 0 || height <=0) && omx_base->use_state_tuning == FALSE)
  {
    GST_ERROR_OBJECT(self, "we got invalid width or height. sink_setcaps return FALSE");
    return FALSE; /* we can do this only for B2 */
  }

  /* Input port configuration. */
  {
    param.nPortIndex = omx_base->in_port->port_index;
    OMX_GetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);

    param.format.video.nFrameWidth = width;
    param.format.video.nFrameHeight = height;

    gst_structure_get_boolean (structure, "hls_streaming", &hls_streaming);

    gomx->hls_streaming = hls_streaming;

    if(hls_streaming) {
      param.format.video.nFrameWidth = HLS_MAX_WIDTH;
      param.format.video.nFrameHeight = HLS_MAX_HEIGHT;

      GST_WARNING_OBJECT(self, "set output buffer resolution to 720p to handle DRC switch in HLS");
    }

    OMX_SetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);
  }

  if (gomx->component_vendor == GOMX_VENDOR_SLSI_SEC || gomx->component_vendor == GOMX_VENDOR_SLSI_EXYNOS)
  {
    /* MODIFICATION: if avi demuxer can not handle b-frame ts reorder */
    gboolean need_ts_reorder = FALSE;
    gst_structure_get_boolean(structure, "ts-linear", &need_ts_reorder);

    if (need_ts_reorder) {
      OMX_ERRORTYPE err = OMX_ErrorNone;
      OMX_INDEXTYPE index = OMX_IndexComponentStartUnused;

      err = OMX_GetExtensionIndex(gomx->omx_handle, "OMX.SEC.index.enableTimestampReorder", &index);

      if (err == OMX_ErrorNone && index != OMX_IndexComponentStartUnused)
      {
        EnableTimestampReorderParams param;
        G_OMX_INIT_PARAM (param);

        GST_INFO_OBJECT(self, "set OMX_IndexParamEnableTimestampReorder");

        param.bEnable = OMX_TRUE;
        err = OMX_SetParameter(gomx->omx_handle, index, &param);
        if (err != OMX_ErrorNone)
        {
          GST_ERROR_OBJECT(self, "setParam OMX_IndexParamEnableTimestampReorder failed with error (0x%x)", err);
        }
      }
      else
      {
        GST_ERROR_OBJECT(self, "caps has ts-linear but can not set OMX_IndexParamEnableTimestampReorder");
      }
    }
  }

  return gst_pad_set_caps (pad, caps);
}

static void
omx_setup (GstOmxBaseFilter * omx_base)
{
  GstOmxBaseVideoDec *self;
  GOmxCore *gomx;

  self = GST_OMX_BASE_VIDEODEC (omx_base);
  gomx = (GOmxCore *) omx_base->gomx;

  GST_INFO_OBJECT (omx_base, "begin");

  {
    OMX_PARAM_PORTDEFINITIONTYPE param;

    G_OMX_INIT_PARAM (param);

    /* Input port configuration. */
    {
      param.nPortIndex = omx_base->in_port->port_index;
      OMX_GetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);

      param.format.video.eCompressionFormat = self->compression_format;

      OMX_SetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);
    }
  }

  if (omx_base->gomx->component_vendor  == GOMX_VENDOR_SLSI_EXYNOS) {
    /* get extension index and set platform specific buffer enable */
    set_enable_platformSpecificBuffer (omx_base);
  }

  GST_INFO_OBJECT (omx_base, "end");
}

static void
instance_private_values_init(GstElement * element)
{
  GstOmxBaseFilter *omx_base;

  omx_base = GST_OMX_BASE_FILTER (element);

  GST_WARNING("base video dec");

  omx_base->gomx->settings_changed_cb = settings_changed_cb;

  /* MODIFICATION: 3.4 kernel use gem_buffer in decoder: alloc buffer in omx il client for buffer share mode. */
  if (omx_base->gomx->component_vendor  == GOMX_VENDOR_SLSI_EXYNOS && omx_base->out_port->omx_allocate==0) {
    omx_base->out_port->buffer_type = GOMX_BUFFER_GEM_VDEC_OUTPUT;
  } else {
    omx_base->out_port->buffer_type = GOMX_BUFFER_GST;
  }

  omx_base->gomx->codec_type = GSTOMX_CODECTYPE_VIDEO_DEC;
  GST_WARNING("base video dec end");
}

static void
instance_init (GstElement * element)
{
  GST_OMX_BASE_FILTER_CLASS(parent_class)->instance_init(element);

  instance_private_values_init(element);
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *omx_base;

  omx_base = GST_OMX_BASE_FILTER (instance);

  GST_WARNING_OBJECT(omx_base, "begin");
  omx_base->omx_setup = omx_setup;

  gst_pad_set_setcaps_function (omx_base->sinkpad, sink_setcaps);

  instance_private_values_init(GST_ELEMENT(instance));

  GST_WARNING_OBJECT(omx_base, "end");
}
