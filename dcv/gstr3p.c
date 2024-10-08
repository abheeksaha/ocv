#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstr3psrc.h"
#include "gstr3psink.h"

#include "tcptrans.h"


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
r3p_init (GstPlugin * r3p)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template dcv' with your description
   */
  g_print("Init function for r3p plugin!\n") ;
  if (!gst_element_register (r3p, "r3psrc", GST_RANK_NONE, GST_TYPE_R3P_SRC)) return FALSE ;
  if (!gst_element_register (r3p, "r3psink", GST_RANK_NONE, GST_TYPE_R3P_SINK)) return FALSE ;
  return TRUE ;
}

/* PACKAGE: thisobj is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile thisobj code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "r3p"
#endif

/* gstreamer looks for this structure to register dcvs
 *
 * exchange the string 'Template dcv' with your dcv description
 */
#define PACKAGE_VERSION "1.0"
#define GST_LICENSE "GPL"
#define GST_PACKAGE_NAME "r3p"
#define GST_PACKAGE_ORIGIN "http://www.hsc.com"

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    r3p,
    "tcp r3p",
    r3p_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
