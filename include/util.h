/* ------------------------------------------------------------------
 * Pass Note - Utility Functions
 * ------------------------------------------------------------------ */

#include "config.h"

#ifndef PASSNOTE_UTIL_H
#define PASSNOTE_UTIL_H
extern void secure_free_mem ( void *mem, size_t size );
extern void secure_free_string ( char *string );
extern int random_bytes ( void *buffer, size_t length );
#endif
