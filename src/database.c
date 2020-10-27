/* ------------------------------------------------------------------
 * Pass Note - Database Managment
 * ------------------------------------------------------------------ */

#include "config.h"
#include "database.h"
#include "util.h"

#define PASSNOTE_MAGIC { 'P', 'A', 'S', 'S', 'N', 'O', 'T', 'E' }
#define PASSNOTE_MAGIC_SIZE 8

struct linked2_t
{
    struct linked2_t *prev;
    struct linked2_t *next;
    char *name;
};

struct stack_t
{
    uint8_t *mem;
    size_t len;
    size_t size;
};

struct search_ctx_t
{
    int options;
    const char *phrase;
    struct stack_t results;
    struct stack_t indices;
    struct stack_t namelens;
    struct stack_t name;
};

static struct node_t *unpack_node ( struct stack_t *stack, int *has_next );
static int pack_node ( struct stack_t *stack, const struct node_t *node );
static struct field_t *new_field_m ( const char *name, const char *value, int modified );
static int merge_node ( struct node_t *tree, struct node_t *aux, struct database_stats_t *stats );
static void append_child_no_check ( struct holder_t *holder, struct node_t *node, int *position );
static void append_field_no_check ( struct leaf_t *leaf, struct field_t *field, int *position );
static int search_generic ( struct node_t *node, struct search_ctx_t *ctx );

static int stack_clone ( struct stack_t *src, struct stack_t *dst )
{
    if ( !src->mem )
    {
        dst->mem = NULL;

    } else if ( !( dst->mem = ( uint8_t * ) malloc ( src->size ) ) )
    {
        return -1;

    } else
    {
        memcpy ( dst->mem, src->mem, src->len );
    }

    dst->len = src->len;
    dst->size = src->size;

    return 0;
}

static int push_binary ( struct stack_t *stack, const uint8_t * slice, size_t len )
{
    size_t reqlen;
    size_t sizex2;
    uint8_t *mem_backup;
    reqlen = stack->len + len;
    if ( !stack->mem || reqlen > stack->size )
    {
        sizex2 = stack->size << 1;
        stack->size = reqlen > sizex2 ? reqlen : sizex2;
        if ( stack->mem )
        {
            if ( !( mem_backup = ( uint8_t * ) malloc ( stack->len ) ) )
            {
                secure_free_mem ( stack->mem, stack->len );
                return -1;
            }
            memcpy ( mem_backup, stack->mem, stack->len );
            secure_free_mem ( stack->mem, stack->len );
            if ( !( stack->mem = ( uint8_t * ) malloc ( stack->size ) ) )
            {
                secure_free_mem ( mem_backup, stack->len );
                return -1;
            }
            memcpy ( stack->mem, mem_backup, stack->len );
            secure_free_mem ( mem_backup, stack->len );
        } else if ( !( stack->mem = ( uint8_t * ) malloc ( stack->size ) ) )
        {
            return -1;
        }
    }
    memcpy ( stack->mem + stack->len, slice, len );
    stack->len += len;
    return 0;
}

static int push_string_null ( struct stack_t *stack, const char *string )
{
    return push_binary ( stack, ( const uint8_t * ) string, strlen ( string ) + 1 );
}

static int push_string ( struct stack_t *stack, const char *string )
{
    return push_binary ( stack, ( const uint8_t * ) string, strlen ( string ) );
}

static int push_int ( struct stack_t *stack, int value )
{
    char buffer[16];
    if ( snprintf ( buffer, sizeof ( buffer ), "%x", value ) <= 0 )
    {
        return -1;
    }
    return push_string_null ( stack, buffer );
}

static int pack_field ( struct stack_t *stack, const struct field_t *field )
{
    uint8_t array[] = { 'f', 0 };
    array[1] = field->next ? '+' : '-';
    if ( push_binary ( stack, array, sizeof ( array ) ) < 0
        || push_int ( stack, field->modified ) < 0
        || push_string_null ( stack, field->name ) < 0
        || push_string_null ( stack, field->value ) < 0 )
    {
        return -1;
    }
    return 0;
}

static int pack_leaf ( struct stack_t *stack, const struct leaf_t *leaf )
{
    struct field_t *ptr;
    uint8_t array[] = { 'l', 0 };

    array[1] = leaf->next ? '+' : '-';
    if ( push_binary ( stack, array, sizeof ( array ) ) < 0
        || push_string_null ( stack, leaf->name ) < 0 )
    {
        return -1;
    }

    for ( ptr = leaf->fields_head; ptr; ptr = ptr->next )
    {
        if ( pack_field ( stack, ptr ) < 0 )
        {
            return -1;
        }
    }

    return 0;
}

static int pack_holder ( struct stack_t *stack, const struct holder_t *holder )
{
    struct node_t *ptr;
    uint8_t array[2];

    array[0] = holder->children_head ? 'h' : 'e';
    array[1] = holder->next ? '+' : '-';
    if ( push_binary ( stack, array, sizeof ( array ) ) < 0
        || push_string_null ( stack, holder->name ) < 0 )
    {
        return -1;
    }

    for ( ptr = holder->children_head; ptr; ptr = ptr->next )
    {
        if ( pack_node ( stack, ptr ) < 0 )
        {
            return -1;
        }
    }

    return 0;
}

static int pack_node ( struct stack_t *stack, const struct node_t *node )
{
    if ( node->is_leaf )
    {
        return pack_leaf ( stack, ( const struct leaf_t * ) node );
    } else
    {
        return pack_holder ( stack, ( const struct holder_t * ) node );
    }
}

int pack_tree ( const struct node_t *node, uint8_t ** mem, size_t *size )
{
    uint8_t magic[PASSNOTE_MAGIC_SIZE] = PASSNOTE_MAGIC;
    uint8_t zeros[PASSNOTE_MAGIC_SIZE] = { 0 };
    struct stack_t stack = { 0 };

    if ( push_binary ( &stack, magic, sizeof ( magic ) ) < 0
        || pack_node ( &stack, node ) < 0 || push_binary ( &stack, zeros, sizeof ( zeros ) ) < 0 )
    {
        return -1;
    }

    *mem = stack.mem;
    *size = stack.len;
    return 0;
}

static int peek_binary ( struct stack_t *stack, uint8_t * slice, size_t len )
{
    if ( stack->len + len > stack->size )
    {
        errno = ERANGE;
        return -1;
    }
    memcpy ( slice, stack->mem + stack->len, len );
    return 0;
}

static int scan_binary ( struct stack_t *stack, uint8_t * slice, size_t len )
{
    if ( peek_binary ( stack, slice, len ) < 0 )
    {
        return -1;
    }
    stack->len += len;
    return 0;
}

static const char *peek_string ( struct stack_t *stack )
{
    return ( char * ) ( stack->mem + stack->len );
}

static int can_peek_string ( struct stack_t *stack )
{
    return stack->len + strlen ( ( char * ) stack->mem + stack->len ) < stack->size;
}

static void skip_string ( struct stack_t *stack )
{
    stack->len += strlen ( ( char * ) stack->mem + stack->len ) + 1;
}

static int scan_int ( struct stack_t *stack, int *value )
{
    if ( !can_peek_string ( stack ) )
    {
        errno = EMSGSIZE;
        return -1;
    }

    if ( sscanf ( peek_string ( stack ), "%x", ( unsigned int * ) value ) < 0 )
    {
        return -1;
    }
    skip_string ( stack );
    return 0;
}

static int unpack_leaf_has_field ( struct stack_t *stack )
{
    uint8_t array[1];
    if ( peek_binary ( stack, array, sizeof ( array ) ) < 0 )
    {
        return -1;
    }
    return array[0] == 'f';
}

static struct field_t *unpack_field ( struct stack_t *stack, int *has_next )
{
    uint8_t array[2];
    const char *name;
    const char *value;
    int modified;

    if ( scan_binary ( stack, array, sizeof ( array ) ) < 0 || scan_int ( stack, &modified ) < 0 )
    {
        return NULL;
    }

    if ( array[0] != 'f' )
    {
        errno = EINVAL;
        return NULL;
    }

    *has_next = array[1] == '+';

    if ( !can_peek_string ( stack ) )
    {
        errno = EMSGSIZE;
        return NULL;
    }

    name = peek_string ( stack );
    skip_string ( stack );

    if ( !can_peek_string ( stack ) )
    {
        errno = EMSGSIZE;
        return NULL;
    }

    value = peek_string ( stack );
    skip_string ( stack );

    return new_field_m ( name, value, modified );
}

static struct leaf_t *unpack_leaf ( struct stack_t *stack )
{
    int ret;
    struct leaf_t *leaf;
    struct field_t *field;

    if ( !can_peek_string ( stack ) )
    {
        errno = EMSGSIZE;
        return NULL;
    }

    if ( !( leaf = new_leaf ( peek_string ( stack ) ) ) )
    {
        return NULL;
    }

    skip_string ( stack );

    if ( ( ret = unpack_leaf_has_field ( stack ) ) < 0 )
    {
        free_tree ( ( struct node_t * ) leaf );
        return NULL;
    }

    while ( ret )
    {
        if ( !( field = unpack_field ( stack, &ret ) ) )
        {
            free_tree ( ( struct node_t * ) leaf );
            return NULL;
        }
        if ( append_field ( leaf, field ) < 0 )
        {
            free_tree ( ( struct node_t * ) leaf );
            free_field ( field );
            return NULL;
        }
    }

    return leaf;
}

static struct holder_t *unpack_holder ( struct stack_t *stack, int empty )
{
    int has_more_children;
    struct holder_t *holder;
    struct node_t *child;

    if ( !can_peek_string ( stack ) )
    {
        errno = EMSGSIZE;
        return NULL;
    }

    if ( !( holder = new_holder ( peek_string ( stack ) ) ) )
    {
        return NULL;
    }

    skip_string ( stack );
    has_more_children = !empty;

    while ( has_more_children )
    {
        if ( !( child = unpack_node ( stack, &has_more_children ) ) )
        {
            free_tree ( ( struct node_t * ) holder );
            return NULL;
        }
        if ( append_child ( holder, child ) < 0 )
        {
            free_tree ( ( struct node_t * ) holder );
            return NULL;
        }
    }

    return holder;
}

static struct node_t *unpack_node ( struct stack_t *stack, int *has_next )
{
    uint8_t array[2];

    if ( scan_binary ( stack, array, sizeof ( array ) ) < 0 )
    {
        return NULL;
    }

    *has_next = array[1] == '+';

    switch ( array[0] )
    {
    case 'h':
        return ( struct node_t * ) unpack_holder ( stack, FALSE );
    case 'e':
        return ( struct node_t * ) unpack_holder ( stack, TRUE );
    case 'l':
        return ( struct node_t * ) unpack_leaf ( stack );
    default:
        errno = EINVAL;
        return NULL;
    }
}

struct node_t *unpack_tree ( const uint8_t * mem, size_t size )
{
    uint8_t magic[PASSNOTE_MAGIC_SIZE] = PASSNOTE_MAGIC;
    uint8_t magic_check[PASSNOTE_MAGIC_SIZE];
    uint8_t zeros[PASSNOTE_MAGIC_SIZE] = { 0 };
    int has_next;
    struct node_t *result;
    struct stack_t stack = { 0 };

    stack.mem = ( uint8_t * ) mem;
    stack.len = 0;
    stack.size = size;

    if ( scan_binary ( &stack, magic_check, PASSNOTE_MAGIC_SIZE ) < 0 )
    {
        return NULL;
    }

    if ( memcmp ( magic, magic_check, PASSNOTE_MAGIC_SIZE ) )
    {
        errno = EINVAL;
        return NULL;
    }

    if ( stack.size < ( PASSNOTE_MAGIC_SIZE << 1 ) )
    {
        return NULL;
    }

    if ( memcmp ( stack.mem + stack.size - PASSNOTE_MAGIC_SIZE, zeros, PASSNOTE_MAGIC_SIZE ) )
    {
        errno = EACCES;
        return NULL;
    }

    if ( !( result = unpack_node ( &stack, &has_next ) ) )
    {
        return NULL;
    }

    return result;
}

void free_field ( struct field_t *field )
{
    secure_free_string ( field->name );
    secure_free_string ( field->value );
    secure_free_mem ( field, sizeof ( struct field_t ) );
}

static void free_node ( struct node_t *node )
{
    secure_free_string ( node->name );
    secure_free_mem ( node, node->is_leaf ? sizeof ( struct leaf_t ) : sizeof ( struct holder_t ) );
}

static void free_stack ( struct stack_t *stack )
{
    if ( stack->mem )
    {
        secure_free_mem ( stack->mem, stack->size );
        stack->mem = NULL;
        stack->len = 0;
    }
}

static void free_fields ( struct leaf_t *leaf )
{
    struct field_t *ptr;
    struct field_t *next;
    for ( ptr = leaf->fields_head; ptr; ptr = next )
    {
        next = ptr->next;
        free_field ( ptr );
    }
}

static void free_children ( struct holder_t *holder )
{
    struct node_t *ptr;
    struct node_t *next;
    for ( ptr = holder->children_head; ptr; ptr = next )
    {
        next = ptr->next;
        free_tree ( ptr );
    }
}

void free_tree ( struct node_t *node )
{
    if ( node->is_leaf )
    {
        free_fields ( ( struct leaf_t * ) node );
    } else
    {
        free_children ( ( struct holder_t * ) node );
    }
    free_node ( node );
}

static int now ( void )
{
    return time ( NULL );
}

static char *new_substring ( const char *input, size_t len )
{
    char *result;

    if ( !( result = ( char * ) malloc ( len + 1 ) ) )
    {
        return NULL;
    }

    memcpy ( result, input, len );
    result[len] = '\0';
    return result;
}

static char *new_string ( const char *input )
{
    return new_substring ( input, strlen ( input ) );
}

static int can_trim_character ( char c )
{
    return isblank ( c ) || c == '\r' || c == '\n';
}

static char *new_string_trimmed ( const char *input )
{
    size_t len;
    char *result;
    const char *end;

    while ( can_trim_character ( *input ) )
    {
        input++;
    }

    end = input + strlen ( input );

    while ( input + 1 < end && can_trim_character ( end[-1] ) )
    {
        end--;
    }

    len = end - input;

    if ( !( result = ( char * ) malloc ( len + 1 ) ) )
    {
        return NULL;
    }

    memcpy ( result, input, len );
    result[len] = '\0';

    return result;
}

static struct node_t *new_node ( int is_leaf, size_t size, const char *name )
{
    struct node_t *node;

    if ( !( node = ( struct node_t * ) calloc ( 1, size ) ) )
    {
        return NULL;
    }

    if ( !( node->name = new_string_trimmed ( name ) ) )
    {
        free_node ( node );
        return NULL;
    }

    node->is_leaf = is_leaf;
    return node;
}

struct holder_t *new_holder ( const char *name )
{
    return ( struct holder_t * ) new_node ( FALSE, sizeof ( struct holder_t ), name );
}

struct leaf_t *new_leaf ( const char *name )
{
    return ( struct leaf_t * ) new_node ( TRUE, sizeof ( struct leaf_t ), name );
}

static int find_child_by_name ( const struct holder_t *holder, const char *name,
    struct node_t **found )
{
    char *name_trimmed;
    struct node_t *ptr;

    if ( !( name_trimmed = new_string_trimmed ( name ) ) )
    {
        return -1;
    }

    *found = NULL;

    for ( ptr = holder->children_head; ptr; ptr = ptr->next )
    {
        if ( !strcasecmp ( name_trimmed, ptr->name ) )
        {
            *found = ptr;
            break;
        }
    }

    secure_free_string ( name_trimmed );

    return 0;
}

static struct field_t *new_field_m ( const char *name, const char *value, int modified )
{
    struct field_t *field;

    if ( !( field = ( struct field_t * ) calloc ( 1, sizeof ( struct field_t ) ) ) )
    {
        return NULL;
    }

    if ( !( field->name = new_string_trimmed ( name ) ) )
    {
        free_field ( field );
        return NULL;
    }

    if ( !( field->value = new_string_trimmed ( value ) ) )
    {
        free_field ( field );
        return NULL;
    }

    field->modified = modified;
    return field;
}

struct field_t *new_field ( const char *name, const char *value )
{
    return new_field_m ( name, value, now (  ) );
}

int find_field_by_name ( const struct leaf_t *leaf, const char *name, struct field_t **found )
{
    char *name_trimmed;
    struct field_t *ptr;

    if ( !( name_trimmed = new_string_trimmed ( name ) ) )
    {
        return -1;
    }

    *found = NULL;

    for ( ptr = leaf->fields_head; ptr; ptr = ptr->next )
    {
        if ( !strcasecmp ( name_trimmed, ptr->name ) )
        {
            *found = ptr;
            break;
        }
    }

    secure_free_string ( name_trimmed );

    return 0;
}

int edit_field ( struct field_t *field, const char *value )
{
    char *value_alloc;

    if ( !( value_alloc = ( char * ) new_string_trimmed ( value ) ) )
    {
        return -1;
    }

    secure_free_string ( field->value );
    field->value = value_alloc;
    field->modified = now (  );
    return 0;
}

static void linked2_insert ( struct linked2_t **head, struct linked2_t **tail,
    struct linked2_t *node, int *position )
{
    int new_position = 0;
    struct linked2_t *successor;

    if ( !*head )
    {
        node->prev = NULL;
        node->next = NULL;
        *head = node;
        *tail = node;

    } else if ( strcasecmp ( node->name, ( *head )->name ) < 0 )
    {
        node->prev = NULL;
        node->next = *head;
        ( *head )->prev = node;
        *head = node;

    } else if ( strcasecmp ( node->name, ( *tail )->name ) > 0 )
    {
        node->prev = *tail;
        node->next = NULL;
        ( *tail )->next = node;
        *tail = node;
        for ( successor = *head; successor; successor = successor->next )
        {
            if ( successor->next )
            {
                new_position++;
            }
        }

    } else
    {
        successor = ( *head )->next;
        while ( successor && strcasecmp ( successor->name, node->name ) < 0 )
        {
            successor = successor->next;
            new_position++;
        }
        new_position++;
        if ( !successor )
        {
            return;
        }
        successor->prev->next = node;
        node->prev = successor->prev;
        successor->prev = node;
        node->next = successor;
    }

    if ( position )
    {
        *position = new_position;
    }
}

static void linked2_unlink ( struct linked2_t **head, struct linked2_t **tail,
    struct linked2_t *node )
{
    if ( *head )
    {
        if ( *head == node )
        {
            *head = node->next;
        }

        if ( *tail == node )
        {
            *tail = node->prev;
        }

        if ( node->next )
        {
            node->next->prev = node->prev;
        }

        if ( node->prev )
        {
            node->prev->next = node->next;
        }
    }
}

static void linked2_sort ( struct linked2_t **head, struct linked2_t **tail )
{
    struct linked2_t *ptr;
    struct linked2_t *prev;
    struct linked2_t *new_head = NULL;
    struct linked2_t *new_tail = NULL;

    for ( ptr = *tail; ptr; ptr = prev )
    {
        prev = ptr->prev;
        linked2_unlink ( head, tail, ptr );
        linked2_insert ( &new_head, &new_tail, ptr, NULL );
    }

    *head = new_head;
    *tail = new_tail;
}

static void append_child_no_check ( struct holder_t *holder, struct node_t *node, int *position )
{
    linked2_insert ( ( struct linked2_t ** ) &holder->children_head,
        ( struct linked2_t ** ) &holder->children_tail, ( struct linked2_t * ) node, position );
}

int append_child_pos ( struct holder_t *holder, struct node_t *node, int *position )
{
    struct node_t *found;
    if ( find_child_by_name ( holder, node->name, &found ) < 0 )
    {
        return -1;
    }

    if ( found )
    {
        errno = EEXIST;
        return -1;
    }
    append_child_no_check ( holder, node, position );
    return 0;
}

int append_child ( struct holder_t *holder, struct node_t *node )
{
    return append_child_pos ( holder, node, NULL );
}

static void unlink_child ( struct holder_t *holder, struct node_t *node )
{
    linked2_unlink ( ( struct linked2_t ** ) &holder->children_head,
        ( struct linked2_t ** ) &holder->children_tail, ( struct linked2_t * ) node );
}

void delete_child ( struct holder_t *holder, struct node_t *node )
{
    unlink_child ( holder, node );
    free_node ( node );
}

static void append_field_no_check ( struct leaf_t *leaf, struct field_t *field, int *position )
{
    linked2_insert ( ( struct linked2_t ** ) &leaf->fields_head,
        ( struct linked2_t ** ) &leaf->fields_tail, ( struct linked2_t * ) field, position );
}

int append_field_pos ( struct leaf_t *leaf, struct field_t *field, int *position )
{
    struct field_t *found;

    if ( find_field_by_name ( leaf, field->name, &found ) < 0 )
    {
        return -1;
    }

    if ( found )
    {
        errno = EEXIST;
        return -1;
    }

    append_field_no_check ( leaf, field, position );
    return 0;
}

int append_field ( struct leaf_t *leaf, struct field_t *field )
{
    return append_field_pos ( leaf, field, NULL );
}

static void unlink_field ( struct leaf_t *leaf, struct field_t *field )
{
    linked2_unlink ( ( struct linked2_t ** ) &leaf->fields_head,
        ( struct linked2_t ** ) &leaf->fields_tail, ( struct linked2_t * ) field );
}

void delete_field ( struct leaf_t *leaf, struct field_t *field )
{
    unlink_field ( leaf, field );
    free_field ( field );
}

static void merge_fields ( struct leaf_t *a, struct leaf_t *b, struct database_stats_t *stats )
{
    int exists;
    struct field_t *a_ptr;
    struct field_t *b_ptr;
    struct field_t *b_next;

    for ( b_ptr = b->fields_head; b_ptr; b_ptr = b_next )
    {
        b_next = b_ptr->next;
        for ( a_ptr = a->fields_head, exists = FALSE; a_ptr; a_ptr = a_ptr->next )
        {
            if ( !strcasecmp ( a_ptr->name, b_ptr->name ) )
            {
                if ( b_ptr->modified > a_ptr->modified )
                {
                    a_ptr->value = b_ptr->value;
                    a_ptr->modified = b_ptr->modified;
                    b_ptr->value = NULL;
                    stats->fields_updated++;
                }
                exists = TRUE;
                break;
            }
        }

        if ( !exists )
        {
            b_next = b_ptr->next;
            unlink_field ( b, b_ptr );
            append_field_no_check ( a, b_ptr, NULL );
            stats->fields_added++;
        }
    }
}

static int merge_children ( struct holder_t *a, struct holder_t *b, struct database_stats_t *stats )
{
    int exists;
    struct node_t *a_ptr;
    struct node_t *b_ptr;
    struct node_t *b_next;

    for ( b_ptr = b->children_head; b_ptr; b_ptr = b_next )
    {
        b_next = b_ptr->next;
        for ( a_ptr = a->children_head, exists = FALSE; a_ptr; a_ptr = a_ptr->next )
        {
            if ( !strcasecmp ( a_ptr->name, b_ptr->name ) )
            {
                if ( merge_node ( a_ptr, b_ptr, stats ) < 0 )
                {
                    return -1;
                }
                exists = TRUE;
                break;
            }
        }

        if ( !exists )
        {
            b_next = b_ptr->next;
            unlink_child ( b, b_ptr );
            append_child_no_check ( a, b_ptr, NULL );
            if ( b_ptr->is_leaf )
            {
                stats->leaves_added++;
            } else
            {
                stats->holders_added++;
            }
        }
    }

    return 0;
}

static int merge_node ( struct node_t *a, struct node_t *b, struct database_stats_t *stats )
{
    struct node_t *found;
    struct leaf_t tmp;
    if ( a->is_leaf && !b->is_leaf )
    {
        if ( sizeof ( struct leaf_t ) != sizeof ( struct holder_t ) )
        {
            errno = EINVAL;
            return -1;
        }
        memcpy ( &tmp, a, sizeof ( struct leaf_t ) );
        memcpy ( a, b, sizeof ( struct leaf_t ) );
        memcpy ( b, &tmp, sizeof ( struct leaf_t ) );
    }
    if ( !a->is_leaf && b->is_leaf )
    {
        if ( find_child_by_name ( ( struct holder_t * ) a, b->name, &found ) < 0 )
        {
            return -1;
        }
        if ( !found )
        {
            if ( !( found = ( struct node_t * ) new_leaf ( b->name ) ) )
            {
                return -1;
            }
            append_child_no_check ( ( struct holder_t * ) a, found, NULL );
        }
        return merge_node ( found, b, stats );
    }

    if ( a->is_leaf )
    {
        merge_fields ( ( struct leaf_t * ) a, ( struct leaf_t * ) b, stats );
    } else
    {
        return merge_children ( ( struct holder_t * ) a, ( struct holder_t * ) b, stats );
    }
    return 0;
}

int merge_tree ( struct node_t *tree, struct node_t *aux, struct database_stats_t *stats )
{
    int ret;
    ret = merge_node ( tree, aux, stats );
    free_tree ( aux );
    return ret;
}

static struct node_t *find_node_by_path_in ( struct node_t *branch, int *indices, int depth,
    struct node_t **prev_parent, struct node_t **parent )
{
    int position;
    struct node_t *ptr;
    struct holder_t *holder;

    *prev_parent = *parent;
    *parent = branch;

    if ( depth <= 0 || branch->is_leaf )
    {
        return branch;
    }

    holder = ( struct holder_t * ) branch;
    position = 0;
    for ( ptr = holder->children_head; ptr; ptr = ptr->next )
    {
        if ( position == indices[0] )
        {
            return find_node_by_path_in ( ptr, indices + 1, depth - 1, prev_parent, parent );
        }
        position++;
    }

    return NULL;
}

struct node_t *find_node_by_path ( struct node_t *branch, int *indices, int depth,
    struct node_t **parent )
{
    struct node_t *result;
    struct node_t *cur_parent = branch;
    struct node_t *prev_parent = NULL;

    result = find_node_by_path_in ( branch, indices, depth, &prev_parent, &cur_parent );
    *parent = cur_parent == prev_parent ? NULL : prev_parent;

    return result;
}

struct node_t *get_nth_node ( struct holder_t *holder, int index )
{
    struct node_t *ptr;
    int position = 0;
    for ( ptr = holder->children_head; ptr; ptr = ptr->next )
    {
        if ( position == index )
        {
            return ptr;
        }
        position++;
    }
    return NULL;
}

struct field_t *get_nth_field ( struct leaf_t *leaf, int index )
{
    struct field_t *ptr;
    int position = 0;
    for ( ptr = leaf->fields_head; ptr; ptr = ptr->next )
    {
        if ( position == index )
        {
            return ptr;
        }
        position++;
    }
    return NULL;
}

char *copy_as_tsv ( const struct leaf_t *leaf )
{
    uint8_t zero = 0;
    struct stack_t stack = { 0 };
    struct field_t *ptr;

    for ( ptr = leaf->fields_head; ptr; ptr = ptr->next )
    {
        if ( !strcasecmp ( ptr->name, "Session" ) )
        {
            continue;
        }

        if ( push_string ( &stack, ptr->name ) < 0
            || push_string ( &stack, "\t" ) < 0
            || push_string ( &stack, ptr->value ) < 0 || push_string ( &stack, "\n" ) < 0 )
        {
            return NULL;
        }
    }

    if ( push_binary ( &stack, &zero, sizeof ( zero ) ) < 0 )
    {
        return NULL;
    }

    return ( char * ) stack.mem;
}

void paste_as_tsv ( struct leaf_t *leaf, const char *input, struct database_stats_t *stats )
{
    int differs;
    char *name;
    char *value;
    const char *backup;
    struct field_t *field;

    while ( *input )
    {
        while ( *input && *input == '\n' )
        {
            input++;
        }

        if ( !*input )
        {
            break;
        }

        backup = input;

        while ( *input && *input != '\t' )
        {
            input++;
        }

        if ( !*input || *input == '\n' )
        {
            continue;
        }

        if ( !( name = new_substring ( backup, input - backup ) ) )
        {
            return;
        }

        while ( *input && *input == '\t' )
        {
            input++;
        }

        if ( *input == '\n' || !*input )
        {
            secure_free_string ( name );
            break;
        }

        backup = input;

        while ( *input && *input != '\n' )
        {
            input++;
        }

        if ( !( value = new_substring ( backup, input - backup ) ) )
        {
            secure_free_string ( name );
            return;
        }

        if ( find_field_by_name ( leaf, name, &field ) < 0 )
        {
            secure_free_string ( name );
            secure_free_string ( value );
            continue;
        }

        if ( field )
        {
            differs = strcasecmp ( field->value, value );
            if ( edit_field ( field, value ) >= 0 && differs )
            {
                stats->fields_updated++;
            }
            secure_free_string ( name );
            secure_free_string ( value );
        } else
        {
            field = new_field ( name, value );

            secure_free_string ( name );
            secure_free_string ( value );

            if ( !field )
            {
                break;
            }

            if ( append_field ( leaf, field ) >= 0 )
            {
                stats->fields_added++;
            } else
            {
                free_field ( field );
            }
        }
    }
}

int randbyte ( int fd )
{
    unsigned char byte = 0;
    read ( fd, &byte, sizeof ( byte ) );
    return byte;
}

char oneof ( const char *array, int fd )
{
    return array[randbyte ( fd ) % strlen ( array )];
}

#define PASSWORD_UPPER "QWERTYUIOPASDFGHJKLZXCVBNM"
#define PASSWORD_LOWER "qwertyuiopasdfghjklzxcvbnm"
#define PASSWORD_NUMBER "0123456789"
#define PASSWORD_SPECIAL "~!@#$%^&*()_+-={}[];:,./<>?"

int generate_password ( char *result, size_t size )
{
    char backup;
    int fd;
    size_t i;
    size_t j;
    size_t length;
    const char *upper = PASSWORD_UPPER;
    const char *lower = PASSWORD_LOWER;
    const char *number = PASSWORD_NUMBER;
    const char *special = PASSWORD_SPECIAL;
    const char *all = PASSWORD_UPPER PASSWORD_LOWER PASSWORD_NUMBER PASSWORD_SPECIAL;

    if ( ( fd = open ( "/dev/urandom", O_RDONLY ) ) < 0 )
    {
        return -1;
    }

    length = randbyte ( fd ) % 18;
    if ( length < 10 )
    {
        length = 10;
    }

    if ( length >= size )
    {
        close ( fd );
        return -1;
    }

    result[0] = oneof ( upper, fd );
    result[1] = oneof ( lower, fd );
    result[2] = oneof ( number, fd );
    result[3] = oneof ( special, fd );

    for ( i = 4; i < length; i++ )
    {
        result[i] = oneof ( all, fd );
    }

    for ( i = 0; i + 1 < length; i++ )
    {
        j = randbyte ( fd ) * ( length - i ) / 256 + i;
        backup = result[j];
        result[j] = result[i];
        result[i] = backup;
    }

    result[length] = '\0';
    close ( fd );
    return 0;
}

static int search_includes ( int options, const char *haystack, const char *needle )
{
    int found = FALSE;
    size_t i;
    size_t j;
    size_t i_off;
    size_t j_off;
    size_t haystack_len;
    size_t needle_len;

    if ( ~options & SEARCH_IGNORE_WHITESPACES )
    {
        return !!strcasestr ( haystack, needle );
    }

    haystack_len = strlen ( haystack );
    needle_len = strlen ( needle );

    for ( i = 0; i < haystack_len; i++ )
    {
        found = TRUE;
        j = 0;
        i_off = 0;
        j_off = 0;
        while ( j + j_off < needle_len && i + i_off < haystack_len )
        {
            if ( isblank ( needle[j + j_off] ) )
            {
                j_off++;
            } else if ( isblank ( haystack[i + i_off + j] ) )
            {
                i_off++;
            } else if ( tolower ( haystack[i + i_off + j] ) == tolower ( needle[j + j_off] ) )
            {
                j++;
            } else
            {
                found = FALSE;
                break;
            }
        }

        if ( found )
        {
            break;
        }
    }

    return found;
}

int search_fields ( struct leaf_t *leaf, struct search_ctx_t *ctx )
{
    size_t namelen;
    struct field_t *ptr;
    struct search_result_t result;
    struct stack_t current_indices = { 0 };

    if ( ctx->name.len )
    {
        ctx->name.len--;
    }

    namelen = ctx->name.len;

    for ( ptr = leaf->fields_head; ptr; ptr = ptr->next )
    {
        if ( ( ( ctx->options & SEARCH_FIELD_NAME )
                && search_includes ( ctx->options, ptr->name, ctx->phrase ) )
            || ( ( ctx->options & SEARCH_FIELD_VALUE )
                && search_includes ( ctx->options, ptr->value, ctx->phrase ) ) )
        {

            if ( push_string ( &ctx->name, " > " ) < 0
                || push_string_null ( &ctx->name, ptr->name ) < 0 )
            {
                return -1;
            }

            if ( stack_clone ( &ctx->indices, &current_indices ) < 0 )
            {
                return -1;
            }
            result.indices = ( int * ) current_indices.mem;
            result.depth = current_indices.len / sizeof ( int );
            result.name = new_string ( ( char * ) ctx->name.mem );
            result.value = ptr->value;
            if ( push_binary ( &ctx->results, ( uint8_t * ) & result, sizeof ( result ) ) < 0 )
            {
                secure_free_string ( result.name );
                free_stack ( &current_indices );
                return -1;
            }

            ctx->name.mem[namelen] = '\0';
            ctx->name.len = namelen;
        }
    }

    return 0;
}

static int pop_binary ( struct stack_t *stack, uint8_t * slice, size_t len )
{
    if ( stack->len < len )
    {
        errno = EINVAL;
        return -1;
    }

    stack->len -= len;
    if ( slice )
    {
        memcpy ( slice, stack->mem + stack->len, len );
    }
    return 0;
}

int search_children ( struct holder_t *holder, struct search_ctx_t *ctx )
{
    int position = 0;
    int *last_index_ptr;
    size_t index_offset;
    struct node_t *ptr;

    if ( push_binary ( &ctx->indices, ( uint8_t * ) & position, sizeof ( position ) ) < 0 )
    {
        return -1;
    }

    index_offset = ctx->indices.len - sizeof ( int );

    for ( ptr = holder->children_head; ptr; ptr = ptr->next )
    {
        last_index_ptr = ( int * ) ( ctx->indices.mem + index_offset );
        *last_index_ptr = position;
        if ( search_generic ( ( struct node_t * ) ptr, ctx ) < 0 )
        {
            return -1;
        }
        position++;
    }

    if ( pop_binary ( &ctx->indices, NULL, sizeof ( position ) ) < 0 )
    {
        return -1;
    }
    return 0;
}

static int search_generic ( struct node_t *node, struct search_ctx_t *ctx )
{
    int ret = 0;
    int first = TRUE;
    struct search_result_t result;
    struct stack_t current_indices = { 0 };

    if ( ctx->name.len )
    {
        first = FALSE;
        ctx->name.len--;
    }

    if ( push_binary ( &ctx->namelens, ( uint8_t * ) & ctx->name.len,
            sizeof ( ctx->name.len ) ) < 0 )
    {
        return -1;
    }

    if ( !first && push_string ( &ctx->name, " > " ) < 0 )
    {
        return -1;
    }

    if ( push_string ( &ctx->name, node->name ) < 0 )
    {
        return -1;
    }

    if ( push_string_null ( &ctx->name, "" ) < 0 )
    {
        return -1;
    }

    if ( ( ( ( ctx->options & SEARCH_LEAF_NAME ) && node->is_leaf )
            || ( ( ctx->options & SEARCH_HOLDER_NAME ) && !node->is_leaf ) )
        && !first && search_includes ( ctx->options, node->name, ctx->phrase ) )
    {
        if ( stack_clone ( &ctx->indices, &current_indices ) < 0 )
        {
            return -1;
        }
        result.indices = ( int * ) current_indices.mem;
        result.depth = current_indices.len / sizeof ( int );
        result.name = new_string ( ( char * ) ctx->name.mem );
        result.value = NULL;
        if ( push_binary ( &ctx->results, ( uint8_t * ) & result, sizeof ( result ) ) < 0 )
        {
            free_stack ( &current_indices );
            return -1;
        }
    }

    if ( node->is_leaf )
    {
        if ( ctx->options & ( SEARCH_FIELD_NAME | SEARCH_FIELD_VALUE ) )
        {
            ret = search_fields ( ( struct leaf_t * ) node, ctx );
        }
    } else
    {
        ret = search_children ( ( struct holder_t * ) node, ctx );
    }

    if ( pop_binary ( &ctx->namelens, ( uint8_t * ) & ctx->name.len,
            sizeof ( ctx->name.len ) ) < 0 )
    {
        return -1;
    }

    ctx->name.mem[ctx->name.len++] = '\0';
    return ret;
}

int search_run ( struct node_t *tree, int options, const char *phrase,
    struct search_result_t **results, size_t *n )
{
    int ret;
    struct search_ctx_t ctx = { 0 };

    if ( !options )
    {
        *n = 0;
        return 0;
    }

    ctx.options = options;
    ctx.phrase = phrase;

    if ( ( ret = search_generic ( tree, &ctx ) ) >= 0 )
    {
        *results = ( struct search_result_t * ) ctx.results.mem;
    }

    free_stack ( &ctx.name );
    free_stack ( &ctx.namelens );
    free_stack ( &ctx.indices );

    *n = ctx.results.len / sizeof ( struct search_result_t );

    if ( ret < 0 )
    {
        search_free ( ( struct search_result_t ** ) &ctx.results.mem, n );
    }

    return ret;
}

void search_free ( struct search_result_t **results, size_t *n )
{
    size_t i;

    if ( *results && n )
    {
        for ( i = 0; i < *n; i++ )
        {
            secure_free_string ( ( *results )[i].name );
            secure_free_mem ( ( *results )[i].indices, ( *results )[i].depth * sizeof ( int ) );
        }
        secure_free_mem ( *results, ( *n ) * sizeof ( struct search_result_t ) );
        *results = NULL;
    }
}

int rename_node ( struct holder_t *holder, struct node_t *node, const char *name, int *position )
{
    char *name_alloc;
    struct node_t *found;

    if ( ( struct node_t * ) holder == node )
    {
        errno = EINVAL;
        return -1;
    }

    if ( holder )
    {
        if ( find_child_by_name ( holder, name, &found ) < 0 )
        {
            return -1;
        }

        if ( found )
        {
            errno = EEXIST;
            return -1;
        }
    }

    if ( !( name_alloc = ( char * ) new_string ( name ) ) )
    {
        return -1;
    }

    secure_free_string ( node->name );
    node->name = name_alloc;
    if ( holder )
    {
        unlink_child ( holder, node );
        append_child_no_check ( holder, node, position );
    }

    return 0;
}

int rename_field ( struct leaf_t *leaf, struct field_t *field, const char *name, int *position )
{
    char *name_alloc;
    struct field_t *found;

    if ( find_field_by_name ( leaf, name, &found ) < 0 )
    {
        return -1;
    }

    if ( found )
    {
        errno = EEXIST;
        return -1;
    }

    if ( !( name_alloc = ( char * ) new_string_trimmed ( name ) ) )
    {
        return -1;
    }

    secure_free_string ( field->name );
    field->name = name_alloc;
    unlink_field ( leaf, field );
    append_field_no_check ( leaf, field, position );
    return 0;
}

static void sort_leaf_fields ( struct leaf_t *leaf )
{
    linked2_sort ( ( struct linked2_t ** ) &leaf->fields_head,
        ( struct linked2_t ** ) &leaf->fields_tail );
}

static void sort_holder_children ( struct holder_t *holder )
{
    struct node_t *ptr;
    linked2_sort ( ( struct linked2_t ** ) &holder->children_head,
        ( struct linked2_t ** ) &holder->children_tail );
    for ( ptr = holder->children_head; ptr; ptr = ptr->next )
    {
        sort_tree ( ptr );
    }
}

void sort_tree ( struct node_t *node )
{
    if ( node->is_leaf )
    {
        sort_leaf_fields ( ( struct leaf_t * ) node );
    } else
    {
        sort_holder_children ( ( struct holder_t * ) node );
    }
}
