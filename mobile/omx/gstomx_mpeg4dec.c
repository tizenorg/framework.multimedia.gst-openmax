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

#include "gstomx_mpeg4dec.h"
#include "gstomx.h"

GSTOMX_BOILERPLATE (GstOmxMpeg4Dec, gst_omx_mpeg4dec, GstOmxBaseVideoDec,
    GST_OMX_BASE_VIDEODEC_TYPE);

static gboolean init_divx_symbol (GstOmxMpeg4Dec * self)
{
  GST_LOG_OBJECT (self, "mpeg4dec load_divx_symbol enter");

  self->divx_handle = dlopen (DIVX_SDK_PLUGIN_NAME, RTLD_LAZY);
  if (!self->divx_handle) {
    GST_ERROR_OBJECT (self, "dlopen failed [%s]", dlerror());
    goto error_exit;
  }

  self->divx_sym_table.init_decrypt = dlsym (self->divx_handle, "divx_init_decrypt");
  if (!self->divx_sym_table.init_decrypt) {
    GST_ERROR_OBJECT (self, "loading divx_init_decrypt failed : %s", dlerror());
    goto error_exit;
  }
  self->divx_sym_table.commit = dlsym (self->divx_handle, "divx_commit");
  if (!self->divx_sym_table.commit) {
    GST_ERROR_OBJECT (self, "loading divx_commit failed : %s", dlerror());
    goto error_exit;
  }
  self->divx_sym_table.decrypt_video = dlsym (self->divx_handle, "divx_decrypt_video");
  if (!self->divx_sym_table.decrypt_video) {
    GST_ERROR_OBJECT (self, "loading divx_decrypt_video failed : %s", dlerror());
    goto error_exit;
  }
  self->divx_sym_table.prepare_video_bitstream = dlsym (self->divx_handle, "divx_prepare_video_bitstream");
  if (!self->divx_sym_table.prepare_video_bitstream) {
    GST_ERROR_OBJECT (self, "loading divx_prepare_video_bitstream failed : %s", dlerror());
    goto error_exit;
  }
  self->divx_sym_table.finalize = dlsym (self->divx_handle, "divx_finalize");
  if (!self->divx_sym_table.finalize) {
    GST_ERROR_OBJECT (self, "loading divx_finalize failed : %s", dlerror());
    goto error_exit;
  }

  return TRUE;

error_exit:

  if (self->divx_handle) {
    dlclose(self->divx_handle);
    self->divx_handle = NULL;
  }

  return FALSE;
}

static gboolean
init_divx_drm (GstOmxMpeg4Dec * self)
{
  int error = 0;

  GST_LOG_OBJECT (self, "mpeg4dec init_divx_drm enter");

  if (init_divx_symbol(self) == FALSE) {
    GST_ERROR_OBJECT (self, "loading symbol failed....");
    goto error_exit;
  }

  self->drmContext = self->divx_sym_table.init_decrypt (&error);

  if (self->drmContext) {
    GST_DEBUG_OBJECT (self, "%s  init success: drmContext = %p\n", __func__, self->drmContext);
  } else {
    GST_ERROR_OBJECT (self, "%s  failed to init... error code = %d \n", __func__, error);
    goto error_exit;
  }

  error = self->divx_sym_table.commit (self->drmContext);

  if (error == DRM_SUCCESS) {
    GST_DEBUG_OBJECT (self, "%s  commit success: drmContext = %p\n", __func__, self->drmContext);
  } else {
    GST_ERROR_OBJECT (self, "%s  failed to commit... error code = %d \n", __func__, error);
    goto error_exit;
  }

  return TRUE;

error_exit:

  if (self->drmContext)
  {
    self->divx_sym_table.finalize (self->drmContext);
    free(self->drmContext);
    self->drmContext = NULL;
  }

  return FALSE;
}

static GstOmxReturn
process_input_buf (GstOmxBaseFilter * omx_base_filter, GstBuffer **buf)
{
  GstOmxMpeg4Dec *self;

  self = GST_OMX_MPEG4DEC (omx_base_filter);

  GST_LOG_OBJECT (self, "mpeg4dec process_input_buf enter");

  /* decrypt DivX DRM buffer if this is DRM */
  if (self->drmContext) {
    if (DRM_SUCCESS == self->divx_sym_table.decrypt_video (self->drmContext, GST_BUFFER_DATA(*buf), GST_BUFFER_SIZE(*buf))) {
      GST_DEBUG_OBJECT (self, "##### DivX DRM Mode ##### decrypt video success : buffer = %d", GST_BUFFER_SIZE(*buf));
    } else {
      GST_ERROR_OBJECT (self, "##### DivX DRM Mode ##### decrypt video failed : buffer = %d", GST_BUFFER_SIZE(*buf));
    }
  }

/* if you want to use commonly for videodec input, use this */
/*  GST_OMX_BASE_FILTER_CLASS (parent_class)->process_input_buf (omx_base_filter, buf); */

  return GSTOMX_RETURN_OK;
}

static void
print_tag (const GstTagList * list, const gchar * tag, gpointer data)
{
  gint i, count;
  GstOmxMpeg4Dec *self;

  self = GST_OMX_MPEG4DEC (data);

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      if (!gst_tag_list_get_string_index (list, tag, i, &str))
        g_assert_not_reached ();
    } else if (gst_tag_get_type (tag) == GST_TYPE_BUFFER) {
      GstBuffer *img;

      img = gst_value_get_buffer (gst_tag_list_get_value_index (list, tag, i));
      if (img) {
        gchar *caps_str;

        caps_str = GST_BUFFER_CAPS (img) ?
            gst_caps_to_string (GST_BUFFER_CAPS (img)) : g_strdup ("unknown");
        str = g_strdup_printf ("buffer of %u bytes, type: %s",
            GST_BUFFER_SIZE (img), caps_str);
        g_free (caps_str);
      } else {
        str = g_strdup ("NULL buffer");
      }
    } else {
      str = g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (i == 0) {
      GST_LOG_OBJECT(self, "%16s: %s", gst_tag_get_nick (tag), str);

      if (strcmp (gst_tag_get_nick(tag), "DRM DivX") == 0) {
        if (self->drmContext == NULL) {
          GST_LOG_OBJECT(self, "Init divx drm !!!!!!!!!!!!!!!!!!!! [%s]", str);
          if (init_divx_drm (self)) {
            GST_LOG_OBJECT(self, "omx_printtag_init_divx_drm() success");
          } else {
            GST_ERROR_OBJECT(self, "omx_printtag_init_divx_drm() failed");
          }
        } else {
          GST_LOG_OBJECT(self, "Init divx drm is DONE before. so do nothing [%s]", str);
        }
      }
    } else {
      GST_LOG_OBJECT(self, "tag is not DRM Divx");
    }

    g_free (str);
  }

  GST_LOG_OBJECT(self, "print_tag End");
}

static gboolean
mpeg4_pad_event (GstPad * pad, GstEvent * event)
{
  GstOmxMpeg4Dec *self;
  gboolean ret = TRUE;

  self = GST_OMX_MPEG4DEC (GST_OBJECT_PARENT (pad));

  GST_LOG_OBJECT (self, "begin");

  GST_INFO_OBJECT (self, "event: %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
    {
      GstTagList *taglist = NULL;

      GST_LOG_OBJECT (self, "GST_EVENT_TAG");

      gst_event_parse_tag (event, &taglist);
      gst_tag_list_foreach (taglist, print_tag, self);
      gst_event_unref (event);
      ret= FALSE;
      break;
    }
    default:
      ret = TRUE;
      break;
  }
  return ret;
}

static void
finalize (GObject * obj)
{
  GstOmxMpeg4Dec *self;

  self = GST_OMX_MPEG4DEC (obj);

  GST_LOG_OBJECT (self, "mpeg4dec finalize enter");

  if (self->drmContext)
  {
    self->divx_sym_table.finalize (self->drmContext);
    free(self->drmContext);
    self->drmContext = NULL;
  }

  if (self->divx_handle)
  {
    dlclose(self->divx_handle);
    self->divx_handle = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
type_base_init (gpointer g_class)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX IL MPEG-4 video decoder",
      "Codec/Decoder/Video",
      "Decodes video in MPEG-4 format with OpenMAX IL", "Felipe Contreras");

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
  basefilter_class = GST_OMX_BASE_FILTER_CLASS (g_class);

  gobject_class->finalize = finalize;
  basefilter_class->process_input_buf = process_input_buf;
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseVideoDec *omx_base;
  GstOmxBaseFilter *omx_base_filter;

  omx_base = GST_OMX_BASE_VIDEODEC (instance);
  omx_base_filter = GST_OMX_BASE_FILTER (instance);

  omx_base_filter->pad_event = mpeg4_pad_event;
  omx_base->compression_format = OMX_VIDEO_CodingMPEG4;
}
