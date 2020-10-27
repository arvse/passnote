/* ------------------------------------------------------------------
 * Pass Note - Database Storage
 * ------------------------------------------------------------------ */

#include "config.h"
#include "database.h"

#ifndef PASSNOTE_STORAGE_H
#define PASSNOTE_STORAGE_H

extern struct node_t *load_database ( const char *path, const char *password );
extern int save_database ( const char *path, struct node_t *node, const char *password );
extern char *read_plain_file ( const char *path );
extern int write_plain_file ( const char *path, const char *content );

#endif
