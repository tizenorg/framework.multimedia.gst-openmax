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

#include "gstomx_h264dec.h"
#include "gstomx.h"

GSTOMX_BOILERPLATE (GstOmxH264Dec, gst_omx_h264dec, GstOmxBaseVideoDec,
    GST_OMX_BASE_VIDEODEC_TYPE);

static void instance_init (GstElement * element);

/*
 *  description : find stream format(3gpp or nalu)
 *  params      : @self : GstOmxH264Dec, @buf: input gstbuffer in pad_chain
 *  return      : none
 *  comments    : finding whether the stream format of input buf is 3GPP or Elementary Stream(nalu)
 */
static void
check_frame (GstOmxH264Dec *self, GstBuffer * buf)
{
  guint buf_size = GST_BUFFER_SIZE (buf);
  guint8 *buf_data = GST_BUFFER_DATA (buf);
  guint nal_type = 0;
  guint forbidden_zero_bit = 0;
  guint check_byte= 0;

  if (buf_data == NULL || buf_size < GSTOMX_H264_NAL_START_LEN + 1) {
    self->h264Format = GSTOMX_H264_FORMAT_UNKNOWN;
    GST_WARNING_OBJECT(self, "H264 format is unknown");
    return;
  }

  self->h264Format = GSTOMX_H264_FORMAT_PACKETIZED;

  if ((buf_data[0] == 0x00)&&(buf_data[1] == 0x00)&&
    ((buf_data[2] == 0x01)||((buf_data[2] == 0x00)&&(buf_data[3] == 0x01)))) {
    check_byte = (buf_data[2] == 0x01) ? 3 : 4;

    nal_type = (guint)(buf_data[check_byte] & 0x1f);
    forbidden_zero_bit = (guint)(buf_data[check_byte] & 0x80);
    GST_LOG_OBJECT(self, "check first frame: nal_type=%d, forbidden_zero_bit=%d", nal_type, forbidden_zero_bit);

    if (forbidden_zero_bit == 0) {
      /* check nal_unit_type is invaild value: ex) slice, DPA,DPB... */
      if ((0 < nal_type && nal_type <= 15) || nal_type == 19 || nal_type == 20) {
        GST_INFO_OBJECT(self, "H264 format is Byte-stream format");
        self->h264Format = GSTOMX_H264_FORMAT_BYTE_STREAM;
      }
    }
  }

  if (self->h264Format == GSTOMX_H264_FORMAT_PACKETIZED)
    GST_INFO_OBJECT(self, "H264 format is Packetized format");
}

/*
 *  description : convert input 3gpp buffer to nalu based buffer
 *  params      : @self : GstOmxH264Dec, @buf: buffer to be converted
 *  return      : none
 *  comments    : none
 */
static void
convert_frame (GstOmxH264Dec *self, GstBuffer **buf)
{
  OMX_U8 frameType;
  OMX_U32 nalSize = 0;
  OMX_U32 cumulSize = 0;
  OMX_U32 offset = 0;
  OMX_U32 nalHeaderSize = 0;
  OMX_U32 outSize = 0;
  OMX_U8 *frame_3gpp = GST_BUFFER_DATA(*buf);
  OMX_U32 frame_3gpp_size = GST_BUFFER_SIZE(*buf);
  GstBuffer *nalu_next_buf = NULL;
  GstBuffer *nalu_buf = NULL;

  do {
      /* get NAL Length based on length of length*/
      if (self->h264NalLengthSize == 1) {
          nalSize = frame_3gpp[0];
      } else if (self->h264NalLengthSize == 2) {
          nalSize = GSTOMX_H264_RB16(frame_3gpp);
      } else {
          nalSize = GSTOMX_H264_RB32(frame_3gpp);
      }

      GST_LOG_OBJECT(self, "packetized frame size = %d", nalSize);

      frame_3gpp += self->h264NalLengthSize;

      /* Checking frame type */
      frameType = *frame_3gpp & 0x1f;

      switch (frameType)
      {
          case GSTOMX_H264_NUT_SLICE:
             GST_LOG_OBJECT(self, "Frame is non-IDR frame...");
              break;
          case GSTOMX_H264_NUT_IDR:
             GST_LOG_OBJECT(self, "Frame is an IDR frame...");
              break;
          case GSTOMX_H264_NUT_SEI:
             GST_LOG_OBJECT(self, "Found SEI Data...");
              break;
          case GSTOMX_H264_NUT_SPS:
             GST_LOG_OBJECT(self, "Found SPS data...");
              break;
          case GSTOMX_H264_NUT_PPS:
             GST_LOG_OBJECT(self, "Found PPS data...");
              break;
          case GSTOMX_H264_NUT_EOSEQ:
             GST_LOG_OBJECT(self, "End of sequence...");
              break;
          case GSTOMX_H264_NUT_EOSTREAM:
             GST_LOG_OBJECT(self, "End of stream...");
              break;
          case GSTOMX_H264_NUT_DPA:
          case GSTOMX_H264_NUT_DPB:
          case GSTOMX_H264_NUT_DPC:
          case GSTOMX_H264_NUT_AUD:
          case GSTOMX_H264_NUT_FILL:
          case GSTOMX_H264_NUT_MIXED:
              break;
          default:
             GST_INFO_OBJECT(self, "Unknown Frame type: %d. do check_frame one more time with next frame.", frameType);
             self->h264Format = GSTOMX_H264_FORMAT_UNKNOWN;
              goto EXIT;
      }

      /* if nal size is same, we can change only start code */
      if((nalSize + GSTOMX_H264_NAL_START_LEN) == frame_3gpp_size) {
          GST_LOG_OBJECT(self, "only change start code");
          *buf = gst_buffer_make_writable(*buf); /* make writable to support memsrc */
          GSTOMX_H264_WB32(GST_BUFFER_DATA(*buf), 1);
          return;
      }

      /* Convert 3GPP Frame to NALU Frame */
      offset = outSize;
      nalHeaderSize = offset ? 3 : 4;

      outSize += nalSize + nalHeaderSize;

      if (nalSize > frame_3gpp_size) {
          GST_ERROR_OBJECT(self, "out of bounds Error. frame_nalu_size=%d", outSize);
          goto EXIT;
      }

      if (nalu_buf) {
          nalu_next_buf= gst_buffer_new_and_alloc(nalSize + nalHeaderSize);
          if (nalu_next_buf == NULL) {
              GST_ERROR_OBJECT(self, "gst_buffer_new_and_alloc failed.(nalu_next_buf)");
              goto EXIT;
          }
      } else {
          nalu_buf = gst_buffer_new_and_alloc(outSize);
      }

      if (nalu_buf == NULL) {
          GST_ERROR_OBJECT(self, "gst_buffer_new_and_alloc failed.(nalu_buf)");
          goto EXIT;
      }

      if (!offset) {
          memcpy(GST_BUFFER_DATA(nalu_buf)+nalHeaderSize, frame_3gpp, nalSize);
          GSTOMX_H264_WB32(GST_BUFFER_DATA(nalu_buf), 1);
      } else {
          if (nalu_next_buf) {
              GstBuffer *nalu_joined_buf = gst_buffer_join(nalu_buf,nalu_next_buf);
              nalu_buf = nalu_joined_buf;
              nalu_next_buf = NULL;
          }
          memcpy(GST_BUFFER_DATA(nalu_buf)+nalHeaderSize+offset, frame_3gpp, nalSize);
          (GST_BUFFER_DATA(nalu_buf)+offset)[0] = (GST_BUFFER_DATA(nalu_buf)+offset)[1] = 0;
          (GST_BUFFER_DATA(nalu_buf)+offset)[2] = 1;
      }

      frame_3gpp += nalSize;
      cumulSize += nalSize + self->h264NalLengthSize;
      GST_LOG_OBJECT(self, "frame_3gpp_size = %d => frame_nalu_size=%d", frame_3gpp_size, outSize);
  } while (cumulSize < frame_3gpp_size);

  gst_buffer_copy_metadata(nalu_buf, *buf, GST_BUFFER_COPY_ALL);

  gst_buffer_unref (*buf);
  *buf = nalu_buf;

  return;

EXIT:
  if (nalu_buf) { gst_buffer_unref (nalu_buf); }
  GST_ERROR_OBJECT(self, "converting frame error.");

  return;
}

/*
 *  description : convert input 3gpp buffer(codec data) to nalu based buffer
 *  params      : @self : GstOmxH264Dec, @buf: buffer to be converted, @dci_nalu: converted buffer
 *  return         : true on successes / false on failure
 *  comments  : none
 */
static gboolean
convert_dci (GstOmxH264Dec *self, GstBuffer *buf, GstBuffer **dci_nalu)
{
  gboolean ret = FALSE;
  OMX_U8 *out = NULL;
  OMX_U16 unitSize = 0;
  OMX_U32 totalSize = 0;
  OMX_U8 unitNb = 0;
  OMX_U8 spsDone = 0;

  OMX_U8 *pInputStream = GST_BUFFER_DATA(buf);
  OMX_U32 pBuffSize = GST_BUFFER_SIZE(buf);

  const OMX_U8 *extraData = (guchar*)pInputStream + 4;
  static const OMX_U8 naluHeader[GSTOMX_H264_NAL_START_LEN] = {0, 0, 0, 1};

  if (pInputStream != NULL) {
      /* retrieve Length of Length*/
      self->h264NalLengthSize = (*extraData++ & 0x03) + 1;
       GST_INFO("Length Of Length is %d", self->h264NalLengthSize);
      if (self->h264NalLengthSize == 3)
      {
          GST_INFO("LengthOfLength is WRONG...");
          goto EXIT;
      }
     /* retrieve sps and pps unit(s) */
      unitNb = *extraData++ & 0x1f;
      GST_INFO("No. of SPS units = %u", unitNb);
      if (!unitNb) {
          GST_INFO("SPS is not present...");
          goto EXIT;
      }

      while (unitNb--) {
          /* get SPS/PPS data Length*/
          unitSize = GSTOMX_H264_RB16(extraData);

          /* Extra 4 bytes for adding delimiter */
          totalSize += unitSize + GSTOMX_H264_NAL_START_LEN;

          /* Check if SPS/PPS Data Length crossed buffer Length */
          if ((extraData + 2 + unitSize) > (pInputStream + pBuffSize)) {
              GST_INFO("SPS length is wrong in DCI...");
              goto EXIT;
          }

          if (out)
            out = g_realloc(out, totalSize);
          else
            out = g_malloc(totalSize);

          if (!out) {
              GST_INFO("realloc failed...");
              goto EXIT;
          }

          /* Copy NALU header */
          memcpy(out + totalSize - unitSize - GSTOMX_H264_NAL_START_LEN,
              naluHeader, GSTOMX_H264_NAL_START_LEN);

          /* Copy SPS/PPS Length and data */
          memcpy(out + totalSize - unitSize, extraData + GSTOMX_H264_SPSPPS_LEN, unitSize);

          extraData += (GSTOMX_H264_SPSPPS_LEN + unitSize);

          if (!unitNb && !spsDone++)
          {
              /* Completed reading SPS data, now read PPS data */
              unitNb = *extraData++; /* number of pps unit(s) */
              GST_INFO( "No. of PPS units = %d", unitNb);
          }
      }

      *dci_nalu = gst_buffer_new_and_alloc(totalSize);
      if (*dci_nalu == NULL) {
          GST_ERROR_OBJECT(self, " gst_buffer_new_and_alloc failed...\n");
          goto EXIT;
      }

      memcpy(GST_BUFFER_DATA(*dci_nalu), out, totalSize);
      GST_INFO( "Total SPS+PPS size = %d", totalSize);
  }

  ret = TRUE;

EXIT:
  if (out) {
      g_free(out);
  }
  return ret;
}


static GstOmxReturn
process_input_buf (GstOmxBaseFilter * omx_base_filter, GstBuffer **buf)
{
  GstOmxH264Dec *h264_self;

  h264_self = GST_OMX_H264DEC (omx_base_filter);

  if (h264_self->h264Format == GSTOMX_H264_FORMAT_UNKNOWN) {
    check_frame(h264_self, *buf);
  }

  if (h264_self->h264Format == GSTOMX_H264_FORMAT_PACKETIZED) {

    if (omx_base_filter->last_pad_push_return != GST_FLOW_OK ||
        !(omx_base_filter->gomx->omx_state == OMX_StateExecuting ||
            omx_base_filter->gomx->omx_state == OMX_StatePause)) {
        GST_LOG_OBJECT(h264_self, "this frame will not be converted and go to out_flushing");
        return GSTOMX_RETURN_OK;
    }

    GST_LOG_OBJECT(h264_self, "H264 format is Packetized format. convert to Byte-stream format");
    convert_frame(h264_self, buf);
  }

/* if you want to use commonly for videodec input, use this */
/*  GST_OMX_BASE_FILTER_CLASS (parent_class)->process_input_buf (omx_base_filter, buf); */

  return GSTOMX_RETURN_OK;
}


static void
type_base_init (gpointer g_class)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "OpenMAX IL H.264/AVC video decoder",
      "Codec/Decoder/Video",
      "Decodes video in H.264/AVC format with OpenMAX IL", "Felipe Contreras");

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

  GST_WARNING("h264 dec  type_class_init");
  basefilter_class = GST_OMX_BASE_FILTER_CLASS (g_class);

  basefilter_class->process_input_buf = process_input_buf;
  basefilter_class->instance_init = instance_init;
}

/* h264 dec has its own sink_setcaps for supporting nalu convert codec data */
static gboolean
sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstOmxBaseVideoDec *self;
  GstOmxH264Dec *h264_self;
  GstOmxBaseFilter *omx_base;
  GOmxCore *gomx;
  gint width = 0;
  gint height = 0;
  gboolean hls_streaming = FALSE;

  self = GST_OMX_BASE_VIDEODEC (GST_PAD_PARENT (pad));
  h264_self = GST_OMX_H264DEC (GST_PAD_PARENT (pad));
  omx_base = GST_OMX_BASE_FILTER (self);

  g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

  GST_WARNING_OBJECT (self, "setcaps (sink)(h264): %" GST_PTR_FORMAT, caps);

  gomx = (GOmxCore *) omx_base->gomx;

  structure = gst_caps_get_structure (caps, 0);

  if(gst_structure_has_field(structure, "max-width") &&
     gst_structure_has_field(structure, "max-height")) {
    gst_structure_get_int (structure, "max-width", &width);
    gst_structure_get_int (structure, "max-height", &height);
  } else {
    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);
  }

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

  {
    const GValue *codec_data;
    GstBuffer *buffer;
    gboolean ret = FALSE;
    guint8 *buf_data = NULL;

    codec_data = gst_structure_get_value (structure, "codec_data");
    if (codec_data) {
      buffer = gst_value_get_buffer (codec_data);

      buf_data = GST_BUFFER_DATA(buffer);

      if (GST_BUFFER_SIZE(buffer) <= GSTOMX_H264_NAL_START_LEN) {
          GST_ERROR("codec data size (%d) is less than start code length.", GST_BUFFER_SIZE(buffer));
      } else {
        if ((buf_data[0] == 0x00)&&(buf_data[1] == 0x00)&&
           ((buf_data[2] == 0x01)||((buf_data[2] == 0x00)&&(buf_data[3] == 0x01)))) {
             h264_self->h264Format = GSTOMX_H264_FORMAT_BYTE_STREAM;
             GST_INFO_OBJECT(self, "H264 codec_data format is Byte-stream.");
        } else {
           h264_self->h264Format = GSTOMX_H264_FORMAT_PACKETIZED;
           GST_INFO_OBJECT(self, "H264 codec_data format is Packetized.");
        }

        /* if codec data is 3gpp format, convert nalu format */
        if(h264_self->h264Format == GSTOMX_H264_FORMAT_PACKETIZED) {
          GstBuffer *nalu_dci = NULL;

          ret = convert_dci(h264_self, buffer, &nalu_dci);
          if (ret) {
            omx_base->codec_data = nalu_dci;
          } else {
            GST_ERROR_OBJECT(h264_self, "converting dci error.");
            if (nalu_dci) {
              gst_buffer_unref (nalu_dci);
              nalu_dci = NULL;
            }
            omx_base->codec_data = buffer;
            gst_buffer_ref (buffer);
          }

        } else { /* not 3GPP format */
          omx_base->codec_data = buffer;
          gst_buffer_ref (buffer);
        }
      }
      h264_self->h264Format = GSTOMX_H264_FORMAT_UNKNOWN;
    }
  }

  gst_structure_get_boolean (structure, "hls_streaming", &hls_streaming);

  gomx->hls_streaming = hls_streaming;

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

  /* Input port configuration. */
  {
    OMX_PARAM_PORTDEFINITIONTYPE param;
    G_OMX_INIT_PARAM (param);

    param.nPortIndex = omx_base->in_port->port_index;
    OMX_GetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);

    param.format.video.nFrameWidth = width;
    param.format.video.nFrameHeight = height;

    if(hls_streaming) {
      param.format.video.nFrameWidth = HLS_MAX_WIDTH;
      param.format.video.nFrameHeight = HLS_MAX_HEIGHT;

      GST_WARNING_OBJECT(self, "set output buffer resolution to 720p to handle DRC switch in HLS");
    }

    OMX_SetParameter (gomx->omx_handle, OMX_IndexParamPortDefinition, &param);
  }
  return gst_pad_set_caps (pad, caps);
}

static void
instance_private_value_init(GstElement * element)
{
  GstOmxBaseVideoDec *omx_base;
  GstOmxBaseFilter *omx_base_filter;

  omx_base_filter = GST_OMX_BASE_FILTER (element);
  omx_base = GST_OMX_BASE_VIDEODEC (element);

  omx_base->compression_format = OMX_VIDEO_CodingAVC;
  omx_base_filter->gomx->compression_format = OMX_VIDEO_CodingAVC;
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
  GstOmxBaseFilter *omx_base_filter;

  GST_WARNING("h264 dec  begin");
  omx_base_filter = GST_OMX_BASE_FILTER (instance);

  instance_private_value_init(GST_ELEMENT(instance));

  gst_pad_set_setcaps_function (omx_base_filter->sinkpad, sink_setcaps);
}
