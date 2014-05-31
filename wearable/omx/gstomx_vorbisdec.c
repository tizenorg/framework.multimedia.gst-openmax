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

#include "gstomx_vorbisdec.h"
#include "gstomx.h"

GSTOMX_BOILERPLATE (GstOmxVorbisDec, gst_omx_vorbisdec, GstOmxBaseAudioDec,
    GST_OMX_BASE_AUDIODEC_TYPE);

static void instance_init (GstElement * element);

static void
type_base_init (gpointer g_class)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX IL Vorbis audio decoder",
      "Codec/Decoder/Audio",
      "Decodes audio in Vorbis format with OpenMAX IL", "Felipe Contreras");

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

  basefilter_class = GST_OMX_BASE_FILTER_CLASS (g_class);
  basefilter_class->instance_init = instance_init;
}

static void
instance_private_value_init(GstElement * element)
{
  GstOmxBaseFilter *omx_base;

  omx_base = GST_OMX_BASE_FILTER (element);

  omx_base->use_timestamps = FALSE;
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
  instance_private_value_init(GST_ELEMENT(instance));
}
