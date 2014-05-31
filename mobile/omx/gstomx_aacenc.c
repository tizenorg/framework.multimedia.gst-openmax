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

#include "gstomx_aacenc.h"
#include "gstomx_base_filter.h"
#include "gstomx.h"

enum
{
  ARG_0,
  ARG_BITRATE,
  ARG_PROFILE,
  ARG_OUTPUT_FORMAT,
};

#define DEFAULT_BITRATE 64000
#define DEFAULT_PROFILE OMX_AUDIO_AACObjectLC
#define DEFAULT_OUTPUT_FORMAT OMX_AUDIO_AACStreamFormatRAW

GSTOMX_BOILERPLATE (GstOmxAacEnc, gst_omx_aacenc, GstOmxBaseFilter,
    GST_OMX_BASE_FILTER_TYPE);

#define GST_TYPE_OMX_AACENC_PROFILE (gst_omx_aacenc_profile_get_type ())
static GType
gst_omx_aacenc_profile_get_type (void)
{
  static GType gst_omx_aacenc_profile_type = 0;

  if (!gst_omx_aacenc_profile_type) {
    static GEnumValue gst_omx_aacenc_profile[] = {
      {OMX_AUDIO_AACObjectLC, "Low Complexity", "LC"},
      {OMX_AUDIO_AACObjectMain, "Main", "Main"},
      {OMX_AUDIO_AACObjectSSR, "Scalable Sample Rate", "SSR"},
      {OMX_AUDIO_AACObjectLTP, "Long Term Prediction", "LTP"},
      {OMX_AUDIO_AACObjectHE, "High Efficiency with SBR (HE-AAC v1)", "HE"},
      {OMX_AUDIO_AACObjectScalable, "Scalable", "Scalable"},
      {OMX_AUDIO_AACObjectERLC,
          "ER AAC Low Complexity object (Error Resilient AAC-LC)", "ERLC"},
      {OMX_AUDIO_AACObjectLD, "AAC Low Delay object (Error Resilient)", "LD"},
      {OMX_AUDIO_AACObjectHE_PS,
            "High Efficiency with Parametric Stereo coding (HE-AAC v2, object type PS)",
          "HE_PS"},
      {0, NULL, NULL},
    };

    gst_omx_aacenc_profile_type = g_enum_register_static ("GstOmxAacencProfile",
        gst_omx_aacenc_profile);
  }

  return gst_omx_aacenc_profile_type;
}

#define GST_TYPE_OMX_AACENC_OUTPUT_FORMAT (gst_omx_aacenc_output_format_get_type ())
static GType
gst_omx_aacenc_output_format_get_type (void)
{
  static GType gst_omx_aacenc_output_format_type = 0;

  if (!gst_omx_aacenc_output_format_type) {
    static GEnumValue gst_omx_aacenc_output_format[] = {
      {OMX_AUDIO_AACStreamFormatMP2ADTS, "Audio Data Transport Stream 2 format",
          "MP2ADTS"},
      {OMX_AUDIO_AACStreamFormatMP4ADTS, "Audio Data Transport Stream 4 format",
          "MP4ADTS"},
      {OMX_AUDIO_AACStreamFormatMP4LOAS, "Low Overhead Audio Stream format",
          "MP4LOAS"},
      {OMX_AUDIO_AACStreamFormatMP4LATM,
          "Low overhead Audio Transport Multiplex", "MP4LATM"},
      {OMX_AUDIO_AACStreamFormatADIF, "Audio Data Interchange Format", "ADIF"},
      {OMX_AUDIO_AACStreamFormatMP4FF, "AAC inside MPEG-4/ISO File Format",
          "MP4FF"},
      {OMX_AUDIO_AACStreamFormatRAW, "AAC Raw Format", "RAW"},
      {0, NULL, NULL},
    };

    gst_omx_aacenc_output_format_type =
        g_enum_register_static ("GstOmxAacencOutputFormat",
        gst_omx_aacenc_output_format);
  }

  return gst_omx_aacenc_output_format_type;
}

static void
type_base_init (gpointer g_class)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX IL AAC audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio in AAC format with OpenMAX IL", "Felipe Contreras");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gstomx_template_caps (G_TYPE_FROM_CLASS (g_class), "sink")));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gstomx_template_caps (G_TYPE_FROM_CLASS (g_class), "src")));
}

static void
set_property (GObject * obj,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOmxAacEnc *self;

  self = GST_OMX_AACENC (obj);

  switch (prop_id) {
    case ARG_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    case ARG_PROFILE:
      self->profile = g_value_get_enum (value);
      break;
    case ARG_OUTPUT_FORMAT:
      self->output_format = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
  }
}

static void
get_property (GObject * obj, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstOmxAacEnc *self;

  self = GST_OMX_AACENC (obj);

  switch (prop_id) {
    case ARG_BITRATE:
            /** @todo propagate this to OpenMAX when processing. */
      g_value_set_uint (value, self->bitrate);
      break;
    case ARG_PROFILE:
      g_value_set_enum (value, self->profile);
      break;
    case ARG_OUTPUT_FORMAT:
      g_value_set_enum (value, self->output_format);
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

  gobject_class = G_OBJECT_CLASS (g_class);

  /* Properties stuff */
  {
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    g_object_class_install_property (gobject_class, ARG_BITRATE,
        g_param_spec_uint ("bitrate", "Bit-rate",
            "Encoding bit-rate",
            0, G_MAXUINT, DEFAULT_BITRATE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject_class, ARG_PROFILE,
        g_param_spec_enum ("profile", "Enocding profile",
            "OMX_AUDIO_AACPROFILETYPE of output",
            GST_TYPE_OMX_AACENC_PROFILE,
            DEFAULT_PROFILE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, ARG_OUTPUT_FORMAT,
        g_param_spec_enum ("output-format", "Output format",
            "OMX_AUDIO_AACSTREAMFORMATTYPE of output",
            GST_TYPE_OMX_AACENC_OUTPUT_FORMAT,
            DEFAULT_OUTPUT_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }
}

static gboolean
sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstOmxBaseFilter *omx_base;
  GOmxCore *gomx;
  gint rate = 0;
  gint channels = 0;

  omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
  gomx = (GOmxCore *) omx_base->gomx;

  GST_INFO_OBJECT (omx_base, "setcaps (sink): %" GST_PTR_FORMAT, caps);

  g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &rate);
  gst_structure_get_int (structure, "channels", &channels);

  /* Input port configuration. */
  {
    OMX_AUDIO_PARAM_PCMMODETYPE param;

    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->in_port->port_index;
    OMX_GetParameter (gomx->omx_handle, OMX_IndexParamAudioPcm, &param);

    param.nSamplingRate = rate;
    param.nChannels = channels;

    OMX_SetParameter (gomx->omx_handle, OMX_IndexParamAudioPcm, &param);
  }

  {
    GstCaps *src_caps;

    src_caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 4,
        "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);
    GST_INFO_OBJECT (omx_base, "src caps are: %" GST_PTR_FORMAT, src_caps);

    gst_pad_set_caps (omx_base->srcpad, src_caps);

    gst_caps_unref (src_caps);
  }

  return gst_pad_set_caps (pad, caps);
}

static void
omx_setup (GstOmxBaseFilter * omx_base)
{
  GstOmxAacEnc *self;
  GOmxCore *gomx;

  self = GST_OMX_AACENC (omx_base);
  gomx = (GOmxCore *) omx_base->gomx;

  GST_INFO_OBJECT (omx_base, "begin");

  {
    OMX_AUDIO_PARAM_AACPROFILETYPE param;

    G_OMX_INIT_PARAM (param);

    /* Output port configuration. */
    {
      param.nPortIndex = omx_base->out_port->port_index;
      OMX_GetParameter (gomx->omx_handle, OMX_IndexParamAudioAac, &param);

      GST_DEBUG_OBJECT (omx_base, "setting bitrate: %i", self->bitrate);
      param.nBitRate = self->bitrate;

      GST_DEBUG_OBJECT (omx_base, "setting profile: %i", self->profile);
      param.eAACProfile = self->profile;

      GST_DEBUG_OBJECT (omx_base, "setting output format: %i",
          self->output_format);
      param.eAACStreamFormat = self->output_format;

      OMX_SetParameter (gomx->omx_handle, OMX_IndexParamAudioAac, &param);
    }
  }

  /* some workarounds. */
#if 0
  {
    OMX_AUDIO_PARAM_PCMMODETYPE param;

    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->in_port->port_index;
    OMX_GetParameter (omx_base->gomx->omx_handle, OMX_IndexParamAudioPcm,
        &param);

    rate = param.nSamplingRate;
    channels = param.nChannels;
  }

  {
    OMX_AUDIO_PARAM_AACPROFILETYPE param;

    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->out_port->port_index;
    OMX_GetParameter (omx_base->gomx->omx_handle, OMX_IndexParamAudioAac,
        &param);

    param.nSampleRate = rate;
    param.nChannels = channels;

    OMX_SetParameter (omx_base->gomx->omx_handle, OMX_IndexParamAudioAac,
        &param);
  }
#endif

  GST_INFO_OBJECT (omx_base, "end");
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *omx_base;
  GstOmxAacEnc *self;

  omx_base = GST_OMX_BASE_FILTER (instance);
  self = GST_OMX_AACENC (instance);

  omx_base->omx_setup = omx_setup;

  gst_pad_set_setcaps_function (omx_base->sinkpad, sink_setcaps);

  self->bitrate = DEFAULT_BITRATE;
  self->profile = DEFAULT_PROFILE;
  self->output_format = DEFAULT_OUTPUT_FORMAT;
}
