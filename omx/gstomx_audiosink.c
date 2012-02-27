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

#include "gstomx_audiosink.h"
#include "gstomx.h"

GSTOMX_BOILERPLATE (GstOmxAudioSink, gst_omx_audiosink, GstOmxBaseSink,
    GST_OMX_BASE_SINK_TYPE);

static void
type_base_init (gpointer g_class)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX IL audiosink element",
      "Sink/Audio", "Renders audio", "Felipe Contreras");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gstomx_template_caps (G_TYPE_FROM_CLASS (g_class), "sink")));
}

static gboolean
setcaps (GstBaseSink * gst_sink, GstCaps * caps)
{
  GstOmxBaseSink *self;
  GOmxCore *gomx;

  self = GST_OMX_BASE_SINK (gst_sink);
  gomx = (GOmxCore *) self->gomx;

  GST_INFO_OBJECT (self, "setcaps (sink): %" GST_PTR_FORMAT, caps);

  g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

  {
    GstStructure *structure;
    gint channels;
    gint width;
    gint rate;
    gboolean is_signed;
    gboolean is_bigendian;

    structure = gst_caps_get_structure (caps, 0);

    gst_structure_get_int (structure, "channels", &channels);
    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "rate", &rate);
    gst_structure_get_boolean (structure, "signed", &is_signed);
    {
      gint endianness;
      gst_structure_get_int (structure, "endianness", &endianness);
      is_bigendian = (endianness == 1234) ? FALSE : TRUE;
    }

    {
      OMX_AUDIO_PARAM_PCMMODETYPE param;

      G_OMX_INIT_PARAM (param);

      param.nPortIndex = self->in_port->port_index;
      OMX_GetParameter (gomx->omx_handle, OMX_IndexParamAudioPcm, &param);

      param.nChannels = channels;
      param.eNumData =
          is_signed ? OMX_NumericalDataSigned : OMX_NumericalDataUnsigned;
      param.eEndian = is_bigendian ? OMX_EndianBig : OMX_EndianLittle;
      param.nBitPerSample = width;
      param.nSamplingRate = rate;

      OMX_SetParameter (gomx->omx_handle, OMX_IndexParamAudioPcm, &param);
    }
  }

  return TRUE;
}

static void
type_class_init (gpointer g_class, gpointer class_data)
{
  GstBaseSinkClass *gst_base_sink_class;

  gst_base_sink_class = GST_BASE_SINK_CLASS (g_class);

  gst_base_sink_class->set_caps = setcaps;
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseSink *omx_base;

  omx_base = GST_OMX_BASE_SINK (instance);

  GST_DEBUG_OBJECT (omx_base, "start");
}
