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

#include "gstomx_mp3dec_alp.h"
#include "gstomx.h"

#define OMX_MP3DEC_OUTBUF_ALP
#define OMX_MP3DEC_INBUF_ALP
#ifdef OMX_MP3DEC_INBUF_ALP
  #define SRP_INPUT_SIZE 0
#else
  #define SRP_INPUT_SIZE 32768
#endif

#define SRP_MP3_FRAME_SIZE 1152
#define SRP_MP3_SAMPLE_SIZE 2

GSTOMX_BOILERPLATE (GstOmxMp3DecAlp, gst_omx_mp3dec_alp, GstOmxBaseAudioDec,
    GST_OMX_BASE_AUDIODEC_TYPE);

static void instance_init (GstElement * element);
static void instance_deinit (GstElement * element);


#ifdef SRP_ENABLE_DUMP
static void init_dump_config(GstOmxMp3DecAlp *self)
{
  dictionary * dict = NULL;

  dict = iniparser_load (SRP_DUMP_INI_DEFAULT_PATH);
  if (!dict) {
    GST_INFO ("%s load failed. Use temporary file", SRP_DUMP_INI_DEFAULT_PATH);
    dict = iniparser_load (SRP_DUMP_INI_TEMP_PATH);
    if (!dict) {
      GST_WARNING ("%s load failed", SRP_DUMP_INI_TEMP_PATH);
      return;
    }
  }

  if (iniparser_getboolean (dict, "pcm_dump:codec", 0) == TRUE) {
    char *suffix, *dump_path;
    GDateTime *time = g_date_time_new_now_local ();

    suffix = g_date_time_format (time, "%Y%m%d_%H%M%S.pcm");
    dump_path = g_strjoin (NULL, SRP_DUMP_INPUT_PATH_PREFIX, suffix, NULL);
    self->pcmFd = fopen (dump_path, "w+");
    g_free (dump_path);
    g_free (suffix);
    g_date_time_unref (time);
    if(!self->pcmFd) {
      GST_ERROR ("Can not create debug dump file");
    }
  }

  iniparser_freedict (dict);
}
#endif

#if 0
static GstOmxReturn
process_output_buf(GstOmxBaseFilter * omx_base, GstBuffer **buf, OMX_BUFFERHEADERTYPE *omx_buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOmxMp3DecAlp *self;
  GstCaps *caps = NULL;
  GstStructure *structure;
  gint samplerate = 0;
  gint channels = 0;
  GstBuffer *push_buf;
  guint offset = 0, output_size;
  GstClockTime timestamp = 0, duration = 0;

  self = GST_OMX_MP3DEC_ALP (omx_base);
  caps = gst_pad_get_negotiated_caps (omx_base->srcpad);
  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "rate", &samplerate);
  gst_structure_get_int (structure, "channels", &channels);
  output_size = SRP_MP3_FRAME_SIZE * SRP_MP3_SAMPLE_SIZE * channels;
  timestamp = GST_BUFFER_TIMESTAMP (*buf);
  duration = GST_SECOND * SRP_MP3_FRAME_SIZE / samplerate;
  gst_caps_unref (caps);
  do {
    /* merge remained data */
    if (self->remain_buffer) {
      if (GST_BUFFER_SIZE(self->remain_buffer) +GST_BUFFER_SIZE(*buf) < output_size) {
        GST_DEBUG_OBJECT (self, "remain_buf (%d) + current_buf (%d) < output_size (%d). change output_size.",
          GST_BUFFER_SIZE(self->remain_buffer), GST_BUFFER_SIZE(*buf), output_size);
        output_size = GST_BUFFER_SIZE(self->remain_buffer) +GST_BUFFER_SIZE(*buf);
      }
      push_buf = gst_buffer_span (self->remain_buffer, 0, *buf, output_size);
      gst_buffer_copy_metadata (push_buf, *buf,
          GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_CAPS);
      GST_BUFFER_DURATION (push_buf) = duration;

      GST_LOG_OBJECT (self, "OUT_BUFFER: remained = %lu size = %lu",
          GST_BUFFER_SIZE (self->remain_buffer), GST_BUFFER_SIZE (push_buf));

      offset += output_size - GST_BUFFER_SIZE (self->remain_buffer);
      gst_buffer_unref (self->remain_buffer);
      self->remain_buffer = NULL;
      ret = gst_pad_push (omx_base->srcpad, push_buf);
      GST_LOG_OBJECT (self, "gst_pad_push end. ret = %d", ret);
    }

    /* separate data by frame size */
    if (GST_BUFFER_SIZE (*buf) - offset >= output_size) {
      push_buf = gst_buffer_create_sub (*buf, offset, output_size);
      gst_buffer_copy_metadata (push_buf, *buf,
          GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_CAPS);
      GST_BUFFER_TIMESTAMP (push_buf) = timestamp
          + (GST_SECOND * offset / (SRP_MP3_SAMPLE_SIZE * channels * samplerate));
      GST_BUFFER_DURATION (push_buf) = duration;

      GST_LOG_OBJECT (self, "OUT_BUFFER: offset = %lu size = %lu",
          offset, GST_BUFFER_SIZE (push_buf));

      offset += output_size;
      ret = gst_pad_push (omx_base->srcpad, push_buf);
      GST_LOG_OBJECT (self, "gst_pad_push end. ret = %d", ret);
    } else {
      /* store remained data */
      if (GST_BUFFER_SIZE (*buf) - offset > 0) {
        self->remain_buffer = gst_buffer_create_sub (*buf, offset,
            GST_BUFFER_SIZE (*buf) - offset);
        GST_BUFFER_TIMESTAMP (self->remain_buffer) = timestamp
            + (GST_SECOND * offset / (SRP_MP3_SAMPLE_SIZE * channels * samplerate));
      }
      break;
    }
  } while (1);

#ifdef SRP_ENABLE_DUMP
  if (self->pcmFd) {
    fwrite (GST_BUFFER_DATA (*buf), 1, GST_BUFFER_SIZE (*buf), self->pcmFd);
  }
#endif

  return GSTOMX_RETURN_SKIP;
}
#endif

static gboolean
pad_event (GstPad * pad, GstEvent * event)
{
  GstOmxBaseFilter *omx_base;
  GstOmxMp3DecAlp *self;
  gboolean ret = TRUE;

  omx_base = GST_OMX_BASE_FILTER (GST_OBJECT_PARENT (pad));
  self = GST_OMX_MP3DEC_ALP (omx_base);

  GST_LOG_OBJECT (self, "begin");

  GST_INFO_OBJECT (self, "event: %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      if (self->remain_buffer) {
        gst_buffer_unref (self->remain_buffer);
        self->remain_buffer = NULL;
      }
      break;

    default:
      break;
  }
  return ret;
}

static void
instance_deinit (GstElement * element)
{
  GstOmxMp3DecAlp *self;
  self = GST_OMX_MP3DEC_ALP (element);

  GST_WARNING_OBJECT (self, "mp3 alp deinit");

  if (self->remain_buffer) {
    gst_buffer_unref (self->remain_buffer);
    self->remain_buffer = NULL;
  }

#ifdef SRP_ENABLE_DUMP
  if (self->pcmFd) {
    fclose (self->pcmFd);
    self->pcmFd = NULL;
  }
#endif

  GST_OMX_BASE_FILTER_CLASS (parent_class)->instance_deinit(element);
}

static void
finalize (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
type_base_init (gpointer g_class)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX IL ALP MP3 audio decoder",
      "Codec/Decoder/Audio/ALP",
      "Decodes audio in MP3 format with OpenMAX IL", "Felipe Contreras");

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
  GObjectClass *gobject_class;
  GstOmxBaseFilterClass *basefilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  gobject_class->finalize = finalize;

  basefilter_class = GST_OMX_BASE_FILTER_CLASS (g_class);
  basefilter_class->instance_init = instance_init;
  basefilter_class->instance_deinit = instance_deinit;
}

static void
instance_private_value_init(GstElement * element)
{
  GstOmxBaseFilter *base_filter;
#ifdef SRP_ENABLE_DUMP
  GstOmxMp3DecAlp *self;
#endif

  base_filter = GST_OMX_BASE_FILTER (element);
#ifdef SRP_ENABLE_DUMP
  self = GST_OMX_MP3DEC_ALP (element);
#endif

  /* ALP audio use adapter */
  base_filter->adapter_size = SRP_INPUT_SIZE;

  GST_INFO_OBJECT (base_filter, "mp3dec ALP. adapter_size=%d", base_filter ->adapter_size);

  self->remain_buffer = NULL;
#ifdef SRP_ENABLE_DUMP
  init_dump_config (self);
#endif
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
  GstOmxBaseFilter *base_filter;

  base_filter = GST_OMX_BASE_FILTER (instance);
  base_filter->pad_event = pad_event;

  instance_private_value_init(GST_ELEMENT(instance));

}
