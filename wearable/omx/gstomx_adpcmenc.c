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

#include "gstomx_adpcmenc.h"
#include "gstomx_base_filter.h"
#include "gstomx.h"

GSTOMX_BOILERPLATE (GstOmxAdpcmEnc, gst_omx_adpcmenc, GstOmxBaseFilter,
    GST_OMX_BASE_FILTER_TYPE);

static void instance_init (GstElement * element);


static void
type_base_init (gpointer g_class)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX IL ADPCM audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio in ADPCM format with OpenMAX IL", "Felipe Contreras");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gstomx_template_caps (G_TYPE_FROM_CLASS (g_class), "sink")));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gstomx_template_caps (G_TYPE_FROM_CLASS (g_class), "src")));
}

static void
type_class_init (gpointer g_class, gpointer class_data)
{
  GstOmxBaseFilterClass *basefilter_class;

  GST_WARNING("ADPCM  type_class_init");
  basefilter_class = GST_OMX_BASE_FILTER_CLASS (g_class);

  basefilter_class->instance_init = instance_init;
}

static void
settings_changed_cb (GOmxCore * core)
{
  GstOmxBaseFilter *omx_base;
  guint rate;

  omx_base = core->object;

  GST_DEBUG_OBJECT (omx_base, "settings changed");

  {
    OMX_AUDIO_PARAM_ADPCMTYPE param;

    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->out_port->port_index;
    OMX_GetParameter (omx_base->gomx->omx_handle, OMX_IndexParamAudioAdpcm,
        &param);

    rate = param.nSampleRate;
  }

  {
    GstCaps *new_caps;

    new_caps = gst_caps_new_simple ("audio/x-adpcm",
        "layout", G_TYPE_STRING, "dvi",
        "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, 1, NULL);

    GST_INFO_OBJECT (omx_base, "caps are: %" GST_PTR_FORMAT, new_caps);
    gst_pad_set_caps (omx_base->srcpad, new_caps);
    gst_caps_unref(new_caps);
  }
}

static gboolean
sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *peer_caps;
  GstStructure *structure;
  GstOmxBaseFilter *omx_base;
  GOmxCore *gomx;
  gint rate = 0;
  gboolean ret = TRUE;

  omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
  gomx = (GOmxCore *) omx_base->gomx;

  GST_INFO_OBJECT (omx_base, "setcaps (sink): %" GST_PTR_FORMAT, caps);

  peer_caps = gst_pad_peer_get_caps (omx_base->srcpad);

  g_return_val_if_fail (peer_caps, FALSE);

  GST_INFO_OBJECT (omx_base, "setcaps (sink): peercaps: %" GST_PTR_FORMAT,
      peer_caps);

  if (gst_caps_get_size (peer_caps) >= 1) {
    structure = gst_caps_get_structure (peer_caps, 0);

    gst_structure_get_int (structure, "rate", &rate);
  } else {
    structure = gst_caps_get_structure (caps, 0);

    gst_structure_get_int (structure, "rate", &rate);
  }

  /* Input port configuration. */
  {
    OMX_AUDIO_PARAM_PCMMODETYPE param;

    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->in_port->port_index;
    OMX_GetParameter (gomx->omx_handle, OMX_IndexParamAudioPcm, &param);

    param.nSamplingRate = rate;

    OMX_SetParameter (gomx->omx_handle, OMX_IndexParamAudioPcm, &param);
  }

  /* set caps on the srcpad */
  {
    GstCaps *tmp_caps;
    GstStructure *tmp_structure;

    tmp_caps = gst_pad_get_allowed_caps (omx_base->srcpad);
    tmp_caps = gst_caps_make_writable (tmp_caps);
    gst_caps_truncate (tmp_caps);

    tmp_structure = gst_caps_get_structure (tmp_caps, 0);
    gst_structure_fixate_field_nearest_int (tmp_structure, "rate", rate);
    gst_pad_fixate_caps (omx_base->srcpad, tmp_caps);

    if (gst_caps_is_fixed (tmp_caps)) {
      GST_INFO_OBJECT (omx_base, "fixated to: %" GST_PTR_FORMAT, tmp_caps);
      gst_pad_set_caps (omx_base->srcpad, tmp_caps);
    }

    gst_caps_unref (tmp_caps);
  }

  ret = gst_pad_set_caps (pad, caps);

  gst_caps_unref (peer_caps);

  return ret;
}

static void
instance_private_value_init(GstElement * element)
{
  GstOmxBaseFilter *omx_base;

  omx_base = GST_OMX_BASE_FILTER (element);

  omx_base->gomx->settings_changed_cb = settings_changed_cb;
}

static void
instance_init (GstElement * element)
{
  GST_OMX_BASE_FILTER_CLASS (parent_class)->instance_init(element);

  instance_private_value_init(element);
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *omx_base;

  omx_base = GST_OMX_BASE_FILTER (instance);

  instance_private_value_init(GST_ELEMENT(instance));

  gst_pad_set_setcaps_function (omx_base->sinkpad, sink_setcaps);
}
