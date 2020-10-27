/* ------------------------------------------------------------------
 * Pass Note - Project Config Header
 * ------------------------------------------------------------------ */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PASSNOTE_CONFIG_H
#define PASSNOTE_CONFIG_H

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

#define APPNAME "Pass Note"
#define APPVER "2.0.11"
#define APPDESC "Lightweight Password Manager"

#ifdef CONFIG_USE_GTK2
#define VBOX_NEW gtk_vbox_new(0, 0)
#else
#define VBOX_NEW gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)
#endif

#ifdef CONFIG_USE_GTK2
#define HBOX_NEW gtk_hbox_new(0, 0)
#else
#define HBOX_NEW gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)
#endif

#define PATH_SIZE 256
#define PASSWORD_SIZE 32

#define g_secure_free_string secure_free_string

#ifdef ENABLE_SESSION
#define SESSION_BROWSER_PATH "bin/browser"
#define SESSION_SYNCUTIL_PATH "bin/syncutil"
#define SESSION_COOKIES_PATH "session"
#define SESSION_FIELD_NAME "Session"
#define SESSION_DEFAULT_PROXY "pab"
#endif

#endif
