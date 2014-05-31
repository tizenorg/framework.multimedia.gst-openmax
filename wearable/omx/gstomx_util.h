/*
 * Copyright (C) 2006-2007 Texas Instruments, Incorporated
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

#ifndef GSTOMX_UTIL_H
#define GSTOMX_UTIL_H

#include <glib.h>
#include <OMX_Core.h>
#include <OMX_Component.h>
#include <async_queue.h>
#include <sem.h>

#define GEM_BUFFER
#ifdef GEM_BUFFER
/* headers for drm and gem */
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include <X11/Xlib.h>
#include <X11/Xlibint.h>

#include <exynos_drm.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <X11/Xmd.h>
#include <dri2/dri2.h>
#include <libdrm/drm.h>
#include <tbm_bufmgr.h>
#endif
#include <gst/gst.h>

/* Typedefs. */

typedef struct GOmxCore GOmxCore;
typedef struct GOmxPort GOmxPort;
typedef struct GOmxImp GOmxImp;
typedef struct GOmxSymbolTable GOmxSymbolTable;
typedef enum GOmxPortType GOmxPortType;


/* MODIFICATION */
typedef enum GOmxVendor GOmxVendor; /* check omx vender */
typedef enum GOmxReconfState GOmxReconfState; /* port setting changed */
typedef enum GOmxPortBufferType GOmxPortBufferType;
typedef struct GstOmxDecOutputBufferObject GstOmxDecOutputBufferObject;
typedef struct GstOmxSendCmdClass GstOmxSendCmdClass;
typedef struct GstOmxSendCmdQueue GstOmxSendCmdQueue;

typedef void (*GOmxCb) (GOmxCore * core);
typedef void (*GOmxPortCb) (GOmxPort * port);

/* MODIFICATION: ignore init fail when going to stop.
 * FIX ME: omx component need to change to handle this. */
#define OMX_ErrorMFCInit 0x90000004
#define OMX_UNRECOVERABLE_ERROR_MAX_COUNT 10
#define OMX_STATE_CHANGE_TIMEOUT 3 /* sec. original source defined 15 sec */

#define TZMEM_IOC_GET_TZMEM 0xC00C5402
#define TZMEM_IOC_GET_FD_PHYS 0xC0085403

#define TZ_VIDEO_INPUTBUFFER_SIZE 5 * 1024 * 1024

/* MODIFICATION: we want to check 10 in/out frames as warning log */
#define MAX_DEBUG_FRAME_CNT 10

struct tzmem_get_region
{
  const char   *key;
  size_t       size;
  int          fd;  
};

struct tzmem_fd_info
{
  int          fd;
  unsigned int  phys_addr;
};

typedef struct private_data
{
  void *buffer;
}private_data;


typedef enum {
       kMetadataBufferTypeCameraSource  = 0,
       kMetadataBufferTypeGrallocSource = 1,
}MetadataBufferType;

typedef struct native_handle
{
    int version;        /* sizeof(native_handle_t) */
    int numFds;         /* number of file-descriptors at &data[0] */
    int numInts;        /* number of ints at &data[numFds] */
    int data[3];        /* numFds + numInts ints */
}native_handle;

typedef struct encoder_media_buffer_type {
       MetadataBufferType buffer_type;
       native_handle *meta_handle;
}encoder_media_buffer_type;

/* Enums. */
enum GOmxPortType
{
  GOMX_PORT_INPUT,
  GOMX_PORT_OUTPUT
};

/* modification: Add_component_vendor */
enum GOmxVendor
{
  GOMX_VENDOR_DEFAULT,
  GOMX_VENDOR_SLSI_SEC,
  GOMX_VENDOR_SLSI_EXYNOS,
};

/* modification: buffer type */
enum GOmxPortBufferType
{
  GOMX_BUFFER_DEFAULT,
  GOMX_BUFFER_GST,
  GOMX_BUFFER_GEM_VDEC_OUTPUT
};

/* MODIFICATION: port setting changed */
enum GOmxReconfState
{
  GOMX_RECONF_STATE_DEFAULT,
  GOMX_RECONF_STATE_START,
  GOMX_RECONF_STATE_PENDING,
  GOMX_RECONF_STATE_DONE
};

#define MAX_GEM_BUFFER_NUM 32 /* FIX ME: need to check max num */

#define ALIGN(x, a)       (((x) + (a) - 1) & ~((a) - 1))

#define S5P_FIMV_DEC_BUF_ALIGN                  (8 * 1024)
#define S5P_FIMV_ENC_BUF_ALIGN                  (8 * 1024)
#define S5P_FIMV_NV12M_HALIGN                   16
#define S5P_FIMV_NV12M_LVALIGN                  16
#define S5P_FIMV_NV12M_CVALIGN                  8
#define S5P_FIMV_NV12MT_HALIGN                  128
#define S5P_FIMV_NV12MT_VALIGN                  64
#define S5P_FIMV_NV12M_SALIGN                   2048
#define S5P_FIMV_NV12MT_SALIGN                  8192

/* using common scmn_imgb format */
#define SCMN_IMGB_MAX_PLANE         (4) /* max channel count */

/* image buffer definition
    +------------------------------------------+ ---
    |                                          |  ^
    |     a[], p[]                             |  |
    |     +---------------------------+ ---    |  |
    |     |                           |  ^     |  |
    |     |<---------- w[] ---------->|  |     |  |
    |     |                           |  |     |  |
    |     |                           |        |
    |     |                           |  h[]   |  e[]
    |     |                           |        |
    |     |                           |  |     |  |
    |     |                           |  |     |  |
    |     |                           |  v     |  |
    |     +---------------------------+ ---    |  |
    |                                          |  v
    +------------------------------------------+ ---

    |<----------------- s[] ------------------>|
*/

enum
{
  BUF_SHARE_METHOD_PADDR = 0,
  BUF_SHARE_METHOD_FD = 1,
  BUF_SHARE_METHOD_TIZEN_BUFFER = 2,
  BUF_SHARE_METHOD_FLUSH_BUFFER = 3,
}; /* buf_share_method */

/* Structures. */

typedef struct
{
    int      w[SCMN_IMGB_MAX_PLANE];    /* width of each image plane */
    int      h[SCMN_IMGB_MAX_PLANE];    /* height of each image plane */
    int      s[SCMN_IMGB_MAX_PLANE];    /* stride of each image plane */
    int      e[SCMN_IMGB_MAX_PLANE];    /* elevation of each image plane */
    void   * a[SCMN_IMGB_MAX_PLANE];    /* user space address of each image plane */
    void   * p[SCMN_IMGB_MAX_PLANE];    /* physical address of each image plane, if needs */
    int      cs;    /* color space type of image */
    int      x;    /* left postion, if needs */
    int      y;    /* top position, if needs */
    int      __dummy2;    /* to align memory */
    int      data[16];    /* arbitrary data */

    /* dmabuf fd */
    int fd[SCMN_IMGB_MAX_PLANE];

    /* flag for buffer share */
    int buf_share_method;

    /* Y plane size in case of ST12 */
    int y_size;
    /* UV plane size in case of ST12 */
    int uv_size;

    /* Tizen buffer object of each image plane */
    void *bo[SCMN_IMGB_MAX_PLANE];

    /* JPEG data */
    void *jpeg_data;
    /* JPEG size */
    int jpeg_size;

    /* tzmem buffer */
    int tz_enable;
} SCMN_IMGB;

struct GstOmxDecOutputBufferObject {
  tbm_bo bo_y;
  tbm_bo bo_uv;
};

struct GOmxSymbolTable
{
  OMX_ERRORTYPE (*init) (void);
  OMX_ERRORTYPE (*deinit) (void);
  OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE * handle,
      OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE * callbacks);
  OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
};

typedef enum GstOmxCodecType
{
    GSTOMX_CODECTYPE_DEFAULT,
    GSTOMX_CODECTYPE_VIDEO_DEC,
    GSTOMX_CODECTYPE_VIDEO_ENC,
    GSTOMX_CODECTYPE_AUDIO_DEC
}GstOmxCodecType;

typedef enum GOmxCommandType
{
    GSTOMX_COMMAND_DEFAULT,
    GSTOMX_COMMAND_PORT_DISABLE,
    GSTOMX_COMMAND_PORT_ENABLE,
    GSTOMX_COMMAND_FREE_BUFFER
}GOmxCommandType;

struct GOmxImp
{
  guint client_count;
  void *dl_handle;
  GOmxSymbolTable sym_table;
  GMutex *mutex;
};

struct GstOmxSendCmdClass
{
  GstTask *cmd_task;
  GStaticRecMutex cmd_mutex;
  AsyncQueue *cmd_queue;
  gboolean cmd_queue_enabled;
};

struct GstOmxSendCmdQueue
{
  GOmxCommandType type;
  guint port;
  OMX_BUFFERHEADERTYPE *omx_buffer;
};

struct GOmxCore
{
  gpointer object;   /**< GStreamer element. */

  OMX_HANDLETYPE omx_handle;
  OMX_ERRORTYPE omx_error;

  OMX_STATETYPE omx_state;
  GCond *omx_state_condition;
  GMutex *omx_state_mutex;

  GPtrArray *ports;

  GSem *done_sem;
  GSem *flush_sem;
  GSem *port_sem;

  GOmxCb settings_changed_cb;
  GOmxImp *imp;

  gboolean done;

  gchar *library_name;
  gchar *component_name;
  gchar *component_role;

  /* MODIFICATION */
  guint input_log_count; /* for debug log */
  guint output_log_count; /* for debug log */

  GOmxVendor component_vendor; /* to check omx vender */

  OMX_VIDEO_CODINGTYPE compression_format; /* this is for Exynos align func */
  GstOmxCodecType codec_type;

  GOmxReconfState reconfiguring; /* portsettingchanged */
  gboolean port_changed;
  gboolean crop_changed; /* crop info changed */


  GstOmxSendCmdClass cmd; /* QC need another thread for handling portsettingchanged event */

  gint omx_unrecover_err_cnt; /* handle continuous MFC init fails */
  gboolean post_gst_element_error; /* to do GST_ELEMENT_ERROR only one time */

  GCond *drc_cond; /* for DRC cond wait */
  GMutex *drc_lock; /* for DRC cond wait */

  OMX_TICKS Last_Ts;
  gboolean hls_streaming;
  GstClockTime previous_ts; /* to avoid sending repeated frames to sink in case of seek */
};

struct GOmxPort
{
  GOmxCore *core;
  GOmxPortType type;

  guint num_buffers;
  gulong buffer_size;
  guint port_index;
  OMX_BUFFERHEADERTYPE **buffers;

  GMutex *mutex;
  gboolean enabled;
  gboolean omx_allocate;   /**< Setup with OMX_AllocateBuffer rather than OMX_UseBuffer */
  AsyncQueue *queue;

  gboolean shared_buffer; /* Modification */
  GstBuffer *initial_pbuffer[MAX_GEM_BUFFER_NUM]; /* FIXME: to free initial pbuffer */

  gint tzmem_fd;
  gint drm_fd;
  tbm_bufmgr bufmgr;
  GOmxPortBufferType buffer_type;
  SCMN_IMGB scmn_out[MAX_GEM_BUFFER_NUM];
  GstOmxDecOutputBufferObject bo[MAX_GEM_BUFFER_NUM];

  gboolean flushing;
  OMX_VIDEO_CODINGTYPE output_color_format;

  gint frame_width;
  gint frame_height;
};

/* Functions. */

void g_omx_init (void);
void g_omx_deinit (void);

GOmxCore *g_omx_core_new (void *object);
void g_omx_core_free (GOmxCore * core);
void g_omx_core_init (GOmxCore * core);
void g_omx_core_prepare (GOmxCore * core);
void g_omx_core_start (GOmxCore * core);
void g_omx_core_pause (GOmxCore * core);
void g_omx_core_stop (GOmxCore * core);
void g_omx_core_unload (GOmxCore * core);
void g_omx_port_clear (GOmxCore * core);
void g_omx_core_set_done (GOmxCore * core);
void g_omx_core_wait_for_done (GOmxCore * core);
void g_omx_core_flush_start (GOmxCore * core);
void g_omx_core_flush_stop (GOmxCore * core);
GOmxPort *g_omx_core_new_port (GOmxCore * core, guint index);

GOmxPort *g_omx_port_new (GOmxCore * core, guint index);
void g_omx_port_free (GOmxPort * port);
void g_omx_port_setup (GOmxPort * port);
void g_omx_port_push_buffer (GOmxPort * port,
    OMX_BUFFERHEADERTYPE * omx_buffer);
OMX_BUFFERHEADERTYPE *g_omx_port_request_buffer (GOmxPort * port);
void g_omx_port_release_buffer (GOmxPort * port,
    OMX_BUFFERHEADERTYPE * omx_buffer);
void g_omx_port_resume (GOmxPort * port);
void g_omx_port_pause (GOmxPort * port);
void g_omx_port_flush (GOmxPort * port);
void g_omx_port_enable (GOmxPort * port);
void g_omx_port_disable (GOmxPort * port);
void g_omx_port_finish (GOmxPort * port);
void g_omx_cmd_queue_finish (GOmxCore * core);

/* Utility Macros */

/**
 * Basically like GST_BOILERPLATE / GST_BOILERPLATE_FULL, but follows the
 * init fxn naming conventions used by gst-openmax.  It expects the following
 * functions to be defined in the same src file following this macro
 * <ul>
 *   <li> type_base_init(gpointer g_class)
 *   <li> type_class_init(gpointer g_class, gpointer class_data)
 *   <li> type_instance_init(GTypeInstance *instance, gpointer g_class)
 * </ul>
 */
#define GSTOMX_BOILERPLATE_FULL(type, type_as_function, parent_type, parent_type_macro, additional_initializations) \
static void type_base_init (gpointer g_class);                                \
static void type_class_init (gpointer g_class, gpointer class_data);          \
static void type_instance_init (GTypeInstance *instance, gpointer g_class);   \
static parent_type ## Class *parent_class;                                    \
static void type_class_init_trampoline (gpointer g_class, gpointer class_data)\
{                                                                             \
    parent_class = g_type_class_ref (parent_type_macro);                      \
    type_class_init (g_class, class_data);                                    \
}                                                                             \
GType type_as_function ## _get_type (void)                                    \
{                                                                             \
    /* The typedef for GType may be gulong or gsize, depending on the         \
     * system and whether the compiler is c++ or not. The g_once_init_*       \
     * functions always take a gsize * though ... */                          \
    static volatile gsize gonce_data = 0;                                     \
    if (g_once_init_enter (&gonce_data)) {                                    \
        GType _type;                                                          \
        GTypeInfo *type_info;                                                 \
        type_info = g_new0 (GTypeInfo, 1);                                    \
        type_info->class_size = sizeof (type ## Class);                       \
        type_info->base_init = type_base_init;                                \
        type_info->class_init = type_class_init_trampoline;                   \
        type_info->instance_size = sizeof (type);                             \
        type_info->instance_init = type_instance_init;                        \
        _type = g_type_register_static (parent_type_macro, #type, type_info, 0);\
        g_free (type_info);                                                   \
        additional_initializations (_type);                                   \
        g_once_init_leave (&gonce_data, (gsize) _type);                       \
    }                                                                         \
    return (GType) gonce_data;                                                \
}

#define GSTOMX_BOILERPLATE(type,type_as_function,parent_type,parent_type_macro)    \
  GSTOMX_BOILERPLATE_FULL (type, type_as_function, parent_type, parent_type_macro, \
      __GST_DO_NOTHING)

#include <string.h>             /* for memset */
#define G_OMX_INIT_PARAM(param) G_STMT_START {                                \
        memset (&(param), 0, sizeof ((param)));                               \
        (param).nSize = sizeof (param);                                       \
        (param).nVersion.s.nVersionMajor = 1;                                 \
        (param).nVersion.s.nVersionMinor = 1;                                 \
    } G_STMT_END


#endif /* GSTOMX_UTIL_H */
