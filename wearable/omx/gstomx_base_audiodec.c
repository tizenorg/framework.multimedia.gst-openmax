/*
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Author: Rob Clark <rob@ti.com>
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

#include "gstomx_base_audiodec.h"
#include "gstomx.h"

enum
{
  ARG_0,
  ARG_USE_STATETUNING, /* STATE_TUNING */
};

GSTOMX_BOILERPLATE (GstOmxBaseAudioDec, gst_omx_base_audiodec, GstOmxBaseFilter,
    GST_OMX_BASE_FILTER_TYPE);


static void instance_init (GstElement * element);


static void
type_base_init (gpointer g_class)
{
}

/* MODIFICATION: add state tuning property */
static void
set_property (GObject * obj,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOmxBaseAudioDec *self;

  self = GST_OMX_BASE_AUDIODEC (obj);

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
  GstOmxBaseAudioDec *self;

  self = GST_OMX_BASE_AUDIODEC (obj);

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

  basefilter_class->instance_init = instance_init;

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
}

static void
settings_changed_cb (GOmxCore * core)
{
  GstOmxBaseFilter *omx_base;
  guint rate;
  guint channels;

  omx_base = core->object;

  GST_DEBUG_OBJECT (omx_base, "settings changed");

  {
    OMX_AUDIO_PARAM_PCMMODETYPE param;

    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->out_port->port_index;
    OMX_GetParameter (omx_base->gomx->omx_handle, OMX_IndexParamAudioPcm,
        &param);

    rate = param.nSamplingRate;
    channels = param.nChannels;
    if (rate == 0) {
            /** @todo: this shouldn't happen. */
      GST_WARNING_OBJECT (omx_base, "Bad samplerate");
      rate = 44100;
    }
  }

  {
    GstCaps *new_caps;

    new_caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, 16,
        "depth", G_TYPE_INT, 16,
        "rate", G_TYPE_INT, rate,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "channels", G_TYPE_INT, channels, NULL);

    GST_INFO_OBJECT (omx_base, "caps are: %" GST_PTR_FORMAT, new_caps);
    gst_pad_set_caps (omx_base->srcpad, new_caps);
    gst_caps_unref(new_caps);
  }
}

static void
instance_private_value_init(GstElement * element)
{
  GstOmxBaseFilter *omx_base;

  omx_base = GST_OMX_BASE_FILTER (element);

  omx_base->gomx->settings_changed_cb = settings_changed_cb;

  omx_base->gomx->codec_type = GSTOMX_CODECTYPE_AUDIO_DEC;
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
