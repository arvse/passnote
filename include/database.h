/* ------------------------------------------------------------------
 * Pass Note - Database Managment
 * ------------------------------------------------------------------ */

#include "config.h"

#ifndef PASSNOTE_DATABASE_H
#define PASSNOTE_DATABASE_H

enum
{
    SEARCH_HOLDER_NAME = 1,
    SEARCH_LEAF_NAME = 2,
    SEARCH_FIELD_NAME = 4,
    SEARCH_FIELD_VALUE = 8,
    SEARCH_IGNORE_WHITESPACES = 16
};

struct field_t
{
    struct field_t *prev;
    struct field_t *next;
    char *name;
    char *value;
    int modified;
};

struct node_t
{
    struct node_t *prev;
    struct node_t *next;
    char *name;
    int is_leaf;
};

struct leaf_t
{
    struct leaf_t *prev;
    struct leaf_t *next;
    char *name;
    int is_leaf;
    struct field_t *fields_head;
    struct field_t *fields_tail;
};

struct holder_t
{
    struct holder_t *prev;
    struct holder_t *next;
    char *name;
    int is_leaf;
    struct node_t *children_head;
    struct node_t *children_tail;
};

struct database_stats_t
{
    int holders_added;
    int leaves_added;
    int fields_added;
    int fields_updated;
};

struct search_result_t
{
    int *indices;
    int depth;
    char *name;
    const char *value;
};

extern int pack_tree ( const struct node_t *node, uint8_t ** mem, size_t *size );
extern struct node_t *unpack_tree ( const uint8_t * mem, size_t size );
extern void free_field ( struct field_t *field );
extern void free_tree ( struct node_t *node );
extern struct holder_t *new_holder ( const char *name );
extern struct leaf_t *new_leaf ( const char *name );
extern int child_exists ( const struct holder_t *holder, const struct node_t *child );
extern int rename_node ( struct holder_t *holder, struct node_t *node, const char *name,
    int *position );
extern struct field_t *new_field ( const char *name, const char *value );
extern int find_field_by_name ( const struct leaf_t *leaf, const char *name,
    struct field_t **found );
extern int rename_field ( struct leaf_t *leaf, struct field_t *field, const char *name,
    int *position );
extern int edit_field ( struct field_t *field, const char *value );
extern int append_child ( struct holder_t *holder, struct node_t *node );
extern int append_child_pos ( struct holder_t *holder, struct node_t *node, int *position );
extern void delete_child ( struct holder_t *holder, struct node_t *node );
extern int append_field ( struct leaf_t *leaf, struct field_t *field );
extern int append_field_pos ( struct leaf_t *leaf, struct field_t *field, int *position );
extern void delete_field ( struct leaf_t *leaf, struct field_t *field );
extern int merge_tree ( struct node_t *tree, struct node_t *aux, struct database_stats_t *stats );
extern struct node_t *find_node_by_path ( struct node_t *branch, int *indices, int depth,
    struct node_t **parent );
extern struct node_t *get_nth_node ( struct holder_t *holder, int index );
extern struct field_t *get_nth_field ( struct leaf_t *leaf, int index );
extern char *copy_as_tsv ( const struct leaf_t *leaf );
extern void paste_as_tsv ( struct leaf_t *leaf, const char *input, struct database_stats_t *stats );
extern int generate_password ( char *result, size_t size );
extern int search_run ( struct node_t *tree, int options, const char *phrase,
    struct search_result_t **results, size_t *n );
extern void search_free ( struct search_result_t **results, size_t *n );
extern void sort_tree ( struct node_t *node );
#endif
