/* ------------------------------------------------------------------
 * Pass Note - User Interface
 * ------------------------------------------------------------------ */

#include "config.h"
#include "storage.h"
#include "util.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms-compat.h>

enum
{
    TABLE_NAME_COLUMN = 0,
    TABLE_VALUE_COLUMN,
    TABLE_NUM_COLS
};

enum
{
    TREE_NAME_COLUMN,
    TREE_NUM_COLS
};

enum
{
    RELOAD_NORMAL,
    RELOAD_SHRINK_PATH,
    RELOAD_DISCARD_PATH
};

struct app_context_t
{
    int modified;
    struct node_t *database;
    struct node_t *node_selected;
    struct node_t *node_parent;
    gchar *last_search_phrase;
    GtkWidget *window;
    GtkWidget *rootbox;
    GtkWidget *authbox;
    GtkWidget *passbox;
    GtkWidget *tree_view;
    GtkWidget *table_view;
    GtkTreeViewColumn *tree_name_column;
    char path[PATH_SIZE];
    char password[PASSWORD_SIZE];
    int search_base_indices[256];
    int search_base_length;
    size_t nresults;
    struct search_result_t *results;
    int include_holder_name;
    int include_leaf_name;
    int include_field_name;
    int include_field_value;
    int ignore_whitespaces;
    int shortpass;
};

static int allow_empty_password_anyway = FALSE;
static struct app_context_t app_context;

extern guchar _binary_res_app_icon_png_start;
extern guchar _binary_res_app_icon_png_end;

static int leaf_selected ( void )
{
    return app_context.node_selected && app_context.node_selected->is_leaf;
}

static struct holder_t *get_insertion_parent ( void )
{
    if ( app_context.node_selected )
    {
        if ( !app_context.node_selected->is_leaf )
        {
            return ( struct holder_t * ) app_context.node_selected;
        }

        if ( app_context.node_parent && !app_context.node_parent->is_leaf )
        {
            return ( struct holder_t * ) app_context.node_parent;
        }
    }

    return NULL;
}

static struct holder_t *get_node_parent ( void )
{
    if ( app_context.node_selected )
    {
        if ( app_context.node_parent && !app_context.node_parent->is_leaf )
        {
            return ( struct holder_t * ) app_context.node_parent;
        }
    }

    return NULL;
}

static void set_modified ( void )
{
    app_context.modified = TRUE;
    gtk_window_set_title ( GTK_WINDOW ( app_context.window ), "*" APPNAME );
}

static void clear_modified ( void )
{
    app_context.modified = FALSE;
    gtk_window_set_title ( GTK_WINDOW ( app_context.window ), APPNAME );
}

static void reset_search ( void )
{
    search_free ( &app_context.results, &app_context.nresults );
}

static void free_database ( void )
{
    if ( app_context.database )
    {
        free_tree ( app_context.database );
        app_context.database = NULL;
    }
}

static int format_time ( const struct timeval *tv, char *str, size_t len )
{
    char format[256];
    time_t time_sec = tv->tv_sec;
    struct tm time_struct;

    if ( localtime_r ( &time_sec, &time_struct ) == NULL )
    {
        return -1;
    }

    if ( tv->tv_usec )
    {
        strftime ( format, sizeof ( format ), "%d-%m-%Y %H:%M:%S", &time_struct );
        snprintf ( str, len, format, tv->tv_usec );

    } else
    {
        strftime ( str, len, "%d-%m-%Y %H:%M:%S", &time_struct );
    }

    return 0;
}

static void select_and_scroll ( GtkTreeView * view, GtkTreeSelection * selection,
    GtkTreePath * path )
{
    gtk_tree_selection_select_path ( selection, path );
    gtk_tree_view_scroll_to_cell ( view, path, NULL, FALSE, 0, 0 );
}

static void select_tree_path ( gint * indices, gint depth, int mode, int position )
{
    gint i;
    GtkTreeSelection *selection;
    GtkTreePath *tmp_path;

    if ( ( tmp_path = gtk_tree_path_new (  ) ) )
    {
        if ( mode != RELOAD_DISCARD_PATH )
        {
            if ( mode == RELOAD_SHRINK_PATH )
            {
                depth--;
            }

            for ( i = 0; i < depth; i++ )
            {
                gtk_tree_path_append_index ( tmp_path, indices[i] );
                gtk_tree_view_expand_row ( GTK_TREE_VIEW ( app_context.tree_view ), tmp_path,
                    FALSE );
            }
        }

        if ( position >= 0 )
        {
            gtk_tree_path_append_index ( tmp_path, position );
            gtk_tree_view_expand_row ( GTK_TREE_VIEW ( app_context.tree_view ), tmp_path, FALSE );
        }

        if ( ( selection =
                gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.tree_view ) ) ) )
        {
            if ( depth > 0 || position >= 0 )
            {
                select_and_scroll ( GTK_TREE_VIEW ( app_context.tree_view ), selection, tmp_path );
            } else
            {
                gtk_tree_selection_unselect_all ( selection );
            }
        }

        gtk_tree_path_free ( tmp_path );
    }
}

static void table_on_row_activated ( GtkTreeView * view, GtkTreePath * path,
    GtkTreeViewColumn * column, gpointer data )
{
    gint index;
    gint *table_path_indices;
    int tree_path_indices[256];
    int i;
    int tree_path_limit;
    int tree_path_length = 0;
    struct search_result_t *result;

    UNUSED ( view );
    UNUSED ( column );
    UNUSED ( data );

    if ( path && app_context.results
        && gtk_tree_path_get_depth ( path ) == 1
        && ( table_path_indices = gtk_tree_path_get_indices ( path ) )
        && ( index = table_path_indices[0] ) >= 0 && index < ( gint ) app_context.nresults )
    {
        result = app_context.results + index;
        tree_path_limit = sizeof ( tree_path_indices ) / sizeof ( int );
        for ( tree_path_length = 0; tree_path_length < app_context.search_base_length &&
            tree_path_length < tree_path_limit; tree_path_length++ )
        {
            tree_path_indices[tree_path_length] = app_context.search_base_indices[tree_path_length];
        }
        for ( i = 0; i < result->depth && tree_path_length < tree_path_limit; i++ )
        {
            tree_path_indices[tree_path_length] = result->indices[i];
            tree_path_length++;
        }
        select_tree_path ( tree_path_indices, tree_path_length, RELOAD_NORMAL, -1 );
    }
}

static GtkWidget *create_table_view_and_model ( void )
{
    GtkCellRenderer *renderer;
    GtkTreeModel *model;
    GtkWidget *view;

    view = gtk_tree_view_new (  );
    gtk_widget_set_can_focus ( view, TRUE );
    g_signal_connect ( view, "row-activated", G_CALLBACK ( table_on_row_activated ), NULL );
    renderer = gtk_cell_renderer_text_new (  );
    gtk_cell_renderer_set_padding ( GTK_CELL_RENDERER ( renderer ), 5, 5 );
    gtk_tree_view_insert_column_with_attributes ( GTK_TREE_VIEW ( view ), -1,
        "Name\t\t\t\t\t\t\t\t\t\t\t\t\t\t", renderer, "text", TABLE_NAME_COLUMN, NULL );

    renderer = gtk_cell_renderer_text_new (  );
    gtk_cell_renderer_set_padding ( GTK_CELL_RENDERER ( renderer ), 5, 5 );
    gtk_tree_view_insert_column_with_attributes ( GTK_TREE_VIEW ( view ), -1,
        "Value", renderer, "text", TABLE_VALUE_COLUMN, NULL );

    model = GTK_TREE_MODEL ( gtk_list_store_new ( TABLE_NUM_COLS, G_TYPE_STRING, G_TYPE_STRING ) );
    gtk_tree_view_set_model ( GTK_TREE_VIEW ( view ), model );
    g_object_unref ( model );
    return view;
}

static void fill_tree_model ( GtkTreeStore * store, GtkTreeIter * parent, struct node_t *node )
{
    struct holder_t *holder;
    struct node_t *ptr;
    GtkTreeIter current;

    if ( parent != ( GtkTreeIter * ) - 1 )
    {
        gtk_tree_store_append ( store, &current, parent );
        gtk_tree_store_set ( store, &current, TREE_NAME_COLUMN, node->name, -1 );
    }

    if ( node && !node->is_leaf )
    {
        holder = ( struct holder_t * ) node;
        for ( ptr = holder->children_head; ptr; ptr = ptr->next )
        {
            fill_tree_model ( store, parent == ( GtkTreeIter * ) - 1 ? NULL : &current, ptr );
        }
    }
}

static const char *get_masked_value ( const char *name, const char *value )
{
    if ( !value[0] )
    {
        return value;
    } else if ( strcasestr ( name, "token" ) || strcasestr ( name, "secret" ) )
    {
        return "****************";
    } else if ( strcasestr ( name, "password" ) || strcasestr ( name, "answer" ) )
    {
        return "**********";
    } else if ( strcasestr ( name, "puk" ) )
    {
        return "********";
    } else if ( strcasestr ( name, "pin" ) )
    {
        return "****";
    } else if ( strcasestr ( name, "cvv" ) )
    {
        return "***";

    } else
    {
        return value;
    }
}

static void update_table ( void )
{
    struct leaf_t *leaf;
    struct field_t *ptr;
    GtkTreeModel *model;
    GtkListStore *store;
    GtkTreeIter iter;

    reset_search (  );
    gtk_tree_view_columns_autosize ( GTK_TREE_VIEW ( app_context.table_view ) );
    model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( app_context.table_view ) );
    store = GTK_LIST_STORE ( model );
    g_object_ref ( model );
    gtk_tree_view_set_model ( GTK_TREE_VIEW ( app_context.table_view ), NULL );
    gtk_list_store_clear ( store );
    if ( leaf_selected (  ) )
    {
        leaf = ( struct leaf_t * ) app_context.node_selected;
        for ( ptr = leaf->fields_head; ptr; ptr = ptr->next )
        {
            gtk_list_store_append ( store, &iter );
            gtk_list_store_set ( store, &iter, TABLE_NAME_COLUMN, ptr->name, TABLE_VALUE_COLUMN,
                get_masked_value ( ptr->name, ptr->value ), -1 );
        }
    }
    gtk_tree_view_set_model ( GTK_TREE_VIEW ( app_context.table_view ), model );
    g_object_unref ( model );
}

static void reload_table ( int position )
{
    GList *temp;
    GList *selected_rows;
    GtkTreePath *path = NULL;
    GtkTreeSelection *selection;
    GtkTreeModel *model = NULL;

    if ( !( selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.table_view ) ) ) )
    {
        update_table (  );
        return;
    }

    if ( position < 0 )
    {
        selected_rows = gtk_tree_selection_get_selected_rows ( selection, &model );
        if ( !selected_rows || !model )
        {
            update_table (  );
            return;
        }
        temp = selected_rows;
        if ( !temp->next )
        {
            path = temp->data;
        }
    }

    update_table (  );

    if ( position >= 0 )
    {
        if ( ( path = gtk_tree_path_new (  ) ) )
        {
            gtk_tree_path_append_index ( path, position );
            select_and_scroll ( GTK_TREE_VIEW ( app_context.table_view ), selection, path );
            gtk_tree_path_free ( path );
        }
    } else
    {
        if ( path )
        {
            select_and_scroll ( GTK_TREE_VIEW ( app_context.table_view ), selection, path );
        }
        g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
    }
}

static void update_tree ( void )
{
    GtkTreeModel *model;
    GtkTreeStore *store;

    reset_search (  );
    gtk_tree_view_column_set_title ( app_context.tree_name_column, app_context.database->name );
    model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( app_context.tree_view ) );
    store = GTK_TREE_STORE ( model );
    g_object_ref ( model );
    gtk_tree_view_set_model ( GTK_TREE_VIEW ( app_context.tree_view ), NULL );
    gtk_tree_store_clear ( store );
    fill_tree_model ( store, ( GtkTreeIter * ) - 1, app_context.database );
    gtk_tree_view_set_model ( GTK_TREE_VIEW ( app_context.tree_view ), model );
    g_object_unref ( model );
}

static GtkTreePath *tree_get_selected_path ( GList ** selected_rows )
{
    GList *temp;
    GtkTreePath *path = NULL;
    GtkTreeSelection *selection;
    GtkTreeModel *model = NULL;

    if ( !( selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.tree_view ) ) ) )
    {
        return NULL;
    }

    *selected_rows = gtk_tree_selection_get_selected_rows ( selection, &model );
    if ( !*selected_rows || !model )
    {
        return NULL;
    }
    temp = *selected_rows;
    if ( !temp->next )
    {
        path = temp->data;
    }
    return path;
}

static void restore_tree_path ( GtkTreePath * path, int mode, int position )
{
    gint depth;
    gint *indices;

    depth = gtk_tree_path_get_depth ( path );

    if ( ( indices = gtk_tree_path_get_indices ( path ) ) && depth > 0 )
    {
        select_tree_path ( indices, depth, mode, position );
    }
}

static void reload_tree ( int mode, int position )
{
    GtkTreePath *path;
    GList *selected_rows = NULL;

    path = tree_get_selected_path ( &selected_rows );
    update_tree (  );

    if ( path )
    {
        restore_tree_path ( path, mode, position );
    }

    if ( selected_rows )
    {
        g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
    }
}

static ssize_t table_get_position_selected ( void )
{
    int result = 0;
    gint *indices;
    GList *temp;
    GList *selected_rows;
    GtkTreePath *path;
    GtkTreeSelection *selection;
    GtkTreeModel *model = NULL;

    if ( !( selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.table_view ) ) ) )
    {
        return -1;
    }

    selected_rows = gtk_tree_selection_get_selected_rows ( selection, &model );
    if ( !selected_rows || !model )
    {
        return -1;
    }

    temp = selected_rows;
    if ( !temp->next )
    {
        path = temp->data;
        if ( ( indices = gtk_tree_path_get_indices ( path ) )
            && gtk_tree_path_get_depth ( path ) == 1 )
        {
            result = indices[0];
        }
    }
    g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
    return result;
}

static struct field_t *table_get_field_selected ( void )
{
    ssize_t position;

    if ( !leaf_selected (  ) )
    {
        return NULL;
    }

    if ( ( position = table_get_position_selected (  ) ) < 0 )
    {
        return NULL;
    }

    return get_nth_field ( ( struct leaf_t * ) app_context.node_selected, position );
}

static void focus_table ( void )
{
    gtk_widget_grab_focus ( app_context.table_view );
}

static void focus_tree ( void )
{
    gtk_widget_grab_focus ( app_context.tree_view );
}

static void infobox ( const char *title, const char *message )
{
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new ( GTK_WINDOW ( app_context.window ),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK, "%s", title );
    gtk_window_set_position ( GTK_WINDOW ( dialog ), GTK_WIN_POS_CENTER );
    gtk_window_set_modal ( GTK_WINDOW ( dialog ), TRUE );
    gtk_window_set_title ( GTK_WINDOW ( dialog ), APPNAME );
    gtk_message_dialog_format_secondary_text ( GTK_MESSAGE_DIALOG ( dialog ), "%s", message );
    gtk_dialog_run ( GTK_DIALOG ( dialog ) );
    gtk_widget_destroy ( dialog );
}

static void success ( const char *message )
{
    infobox ( "Success", message );
}

static void warning ( const char *message )
{
    infobox ( "Warning", message );
}

static void failure ( const char *message )
{
    infobox ( "Failure", message );
}

static int confirm_generic ( const char *message, int yes_default )
{
    gint response;
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new ( GTK_WINDOW ( app_context.window ),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", "" );
    gtk_window_set_position ( GTK_WINDOW ( dialog ), GTK_WIN_POS_CENTER );
    gtk_window_set_modal ( GTK_WINDOW ( dialog ), TRUE );
    gtk_window_set_title ( GTK_WINDOW ( dialog ), APPNAME );
    gtk_dialog_set_default_response ( GTK_DIALOG ( dialog ),
        yes_default ? GTK_RESPONSE_YES : GTK_RESPONSE_NO );
    gtk_message_dialog_format_secondary_text ( GTK_MESSAGE_DIALOG ( dialog ), "%s", message );
    response = gtk_dialog_run ( GTK_DIALOG ( dialog ) );
    gtk_widget_destroy ( dialog );
    return response == GTK_RESPONSE_YES;
}

static int confirm ( const char *message )
{
    return confirm_generic ( message, TRUE );
}

static int confirm_inversed ( const char *message )
{
    return confirm_generic ( message, FALSE );
}

static void tree_on_row_activated ( GtkTreeView * view, GtkTreePath * path,
    GtkTreeViewColumn * column, gpointer data )
{
    UNUSED ( column );
    UNUSED ( data );

    if ( view && path && app_context.node_selected )
    {
        if ( !app_context.node_selected->is_leaf )
        {
            if ( !gtk_tree_view_expand_row ( view, path, FALSE ) )
            {
                gtk_tree_view_collapse_row ( view, path );
            }
        }
    }
}

static gboolean tree_on_key_press ( GtkWidget * widget, GdkEventKey * event, gpointer user_data )
{
    GList *selected_rows = NULL;
    GtkTreePath *path;

    if ( !event )
    {
        return FALSE;
    }

    UNUSED ( widget );
    UNUSED ( user_data );

    if ( ( path = tree_get_selected_path ( &selected_rows ) ) )
    {
        switch ( event->keyval )
        {
        case GDK_Right:
            gtk_tree_view_expand_row ( GTK_TREE_VIEW ( app_context.tree_view ), path, FALSE );
            break;
        case GDK_Left:
            gtk_tree_view_collapse_row ( GTK_TREE_VIEW ( app_context.tree_view ), path );
            break;
        }
    }

    if ( selected_rows )
    {
        g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
    }

    return FALSE;
}

static gboolean tree_selection_func ( GtkTreeSelection * selection,
    GtkTreeModel * model, GtkTreePath * path, gboolean unselect, gpointer data )
{
    gint *indices;
    GtkTreeIter iter;

    UNUSED ( selection );
    UNUSED ( data );

    if ( model && path && gtk_tree_model_get_iter ( model, &iter, path ) )
    {
        if ( !unselect && ( indices = gtk_tree_path_get_indices ( path ) ) )
        {
            app_context.node_selected = find_node_by_path ( app_context.database, indices,
                gtk_tree_path_get_depth ( path ), &app_context.node_parent );
            update_table (  );
        }
    }

    return TRUE;
}

static GtkWidget *create_tree_view_and_model ( void )
{
    GtkCellRenderer *renderer;
    GtkWidget *view;
    GtkTreeModel *model;
    GtkTreeStore *store;
    GtkTreeSelection *selection;

    view = gtk_tree_view_new (  );
    gtk_widget_set_can_focus ( view, TRUE );

    selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW ( view ) );
    gtk_tree_selection_set_select_function ( selection, tree_selection_func, NULL, NULL );
    g_signal_connect ( view, "row-activated", G_CALLBACK ( tree_on_row_activated ), NULL );
    g_signal_connect ( view, "key-press-event", G_CALLBACK ( tree_on_key_press ), NULL );

    app_context.tree_name_column = gtk_tree_view_column_new (  );
    gtk_tree_view_column_set_title ( app_context.tree_name_column, "Unnamed" );
    gtk_tree_view_append_column ( GTK_TREE_VIEW ( view ), app_context.tree_name_column );

    renderer = gtk_cell_renderer_text_new (  );
    gtk_cell_renderer_set_padding ( GTK_CELL_RENDERER ( renderer ), 5, 5 );
    gtk_tree_view_column_pack_start ( app_context.tree_name_column, renderer, TRUE );
    gtk_tree_view_column_add_attribute ( app_context.tree_name_column, renderer, "text",
        TREE_NAME_COLUMN );

    store = gtk_tree_store_new ( TREE_NUM_COLS, G_TYPE_STRING );
    fill_tree_model ( store, ( GtkTreeIter * ) - 1, app_context.database );
    model = GTK_TREE_MODEL ( store );
    gtk_tree_view_set_model ( GTK_TREE_VIEW ( view ), model );
    g_object_unref ( model );

    return view;
}

static void show_database_stats ( const struct database_stats_t *stats, int full )
{
    char buffer[4096];
    buffer[0] = '\0';
    if ( full )
    {
        snprintf ( buffer, sizeof ( buffer ), "Holders added: %i\nLeaves added: %i",
            stats->holders_added, stats->leaves_added );
    }
    snprintf ( buffer + strlen ( buffer ), sizeof ( buffer ) - strlen ( buffer ),
        "%sFields added: %i\nFields updated: %i", full ? "\n" : "",
        stats->fields_added, stats->fields_updated );
    infobox ( "Statistics", buffer );
    memset ( buffer, '\0', strlen ( buffer ) );
}

static void prompt_enter_callback ( GtkWidget * widget, GtkWidget * dialog )
{
    UNUSED ( widget );

    if ( dialog )
    {
        gtk_dialog_response ( GTK_DIALOG ( dialog ), GTK_RESPONSE_OK );
    }
}

static int prompt_generic ( const char *title, gchar ** string, const char *initial, int password )
{
    gint response;
    GtkWidget *dialog;
    GtkWidget *message_area;
    GtkWidget *entry;
    size_t size;
    const gchar *text;
    dialog = gtk_message_dialog_new ( GTK_WINDOW ( app_context.window ),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", "" );
    gtk_window_set_position ( GTK_WINDOW ( dialog ), GTK_WIN_POS_CENTER );
    gtk_window_set_modal ( GTK_WINDOW ( dialog ), TRUE );
    gtk_window_set_title ( GTK_WINDOW ( dialog ), APPNAME );
    gtk_dialog_set_default_response ( GTK_DIALOG ( dialog ), GTK_RESPONSE_OK );
    gtk_message_dialog_format_secondary_text ( GTK_MESSAGE_DIALOG ( dialog ), "%s", title );
    message_area = gtk_message_dialog_get_message_area ( GTK_MESSAGE_DIALOG ( dialog ) );
    entry = gtk_entry_new (  );
    if ( password )
    {
        gtk_entry_set_visibility ( GTK_ENTRY ( entry ), FALSE );
    }
    if ( initial )
    {
        gtk_entry_set_text ( GTK_ENTRY ( entry ), initial );
    }
    g_signal_connect ( entry, "activate", G_CALLBACK ( prompt_enter_callback ), dialog );
    gtk_entry_set_width_chars ( GTK_ENTRY ( entry ), 32 );
    gtk_widget_show ( entry );
    gtk_box_pack_end ( GTK_BOX ( message_area ), entry, TRUE, TRUE, 0 );
    response = gtk_dialog_run ( GTK_DIALOG ( dialog ) );
    if ( response == GTK_RESPONSE_OK && ( text = gtk_entry_get_text ( GTK_ENTRY ( entry ) ) ) )
    {
        size = strlen ( text ) + 1;
        if ( ( *string = ( char * ) malloc ( size ) ) )
        {
            memcpy ( *string, text, size );
        }
    }
    gtk_widget_destroy ( dialog );
    return response == GTK_RESPONSE_OK;
}

static int prompt_text ( const char *title, gchar ** string, const char *initial )
{
    return prompt_generic ( title, string, initial, FALSE );
}

static int prompt_password_generic ( const char *title, gchar ** string, int allow_empty )
{
    gchar c;
    size_t len;
    size_t i;
    int has_upper_case = FALSE;
    int has_lower_case = FALSE;
    int has_digit = FALSE;
    int has_special = FALSE;

    if ( !prompt_generic ( title, string, "", TRUE ) || !*string )
    {
        return FALSE;
    }

    len = strlen ( *string );

    if ( len >= PASSWORD_SIZE )
    {
        g_secure_free_string ( *string );
        *string = NULL;
        failure ( "Password is too long" );
        return FALSE;
    }

    if ( !len && allow_empty )
    {
        warning ( "Empty passwords are INSECURE!" );
        return confirm ( "Process without ANY password?" );
    }

    if ( len < 10 )
    {
        g_secure_free_string ( *string );
        *string = NULL;
        failure ( "Password is too short" );
        return FALSE;
    }

    for ( i = 0; i < len; i++ )
    {
        c = ( *string )[i];
        if ( isupper ( c ) )
        {
            has_upper_case = TRUE;
        } else if ( islower ( c ) )
        {
            has_lower_case = TRUE;
        } else if ( isdigit ( c ) )
        {
            has_digit = TRUE;
        } else
        {
            has_special = TRUE;
        }
    }

    if ( !has_upper_case )
    {
        failure ( "At least one upper case letter!" );
    } else if ( !has_lower_case )
    {
        failure ( "At least one lower case letter!" );
    } else if ( !has_digit )
    {
        failure ( "At least one digit!" );
    } else if ( !has_special )
    {
        failure ( "At least one special character!" );
    }

    if ( !has_upper_case || !has_lower_case || !has_digit || !has_special )
    {
        g_secure_free_string ( *string );
        *string = NULL;
        return FALSE;
    }

    return TRUE;
}

static int prompt_password ( const char *title, gchar ** string )
{
    return prompt_password_generic ( title, string, allow_empty_password_anyway );
}

static int prompt_password_allow_empty ( const char *title, gchar ** string )
{
    return prompt_password_generic ( title, string, TRUE );
}

static int search_prompt ( int *options, const char *title, gchar ** phrase )
{
    gint response;
    GtkWidget *dialog;
    GtkWidget *message_area;
    GtkWidget *entry;
    GtkWidget *include_holder_name;
    GtkWidget *include_leaf_name;
    GtkWidget *include_field_name;
    GtkWidget *include_field_value;
    GtkWidget *ignore_whitespaces;
    size_t size;
    const gchar *text;
    dialog = gtk_message_dialog_new ( GTK_WINDOW ( app_context.window ),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", title );
    gtk_window_set_position ( GTK_WINDOW ( dialog ), GTK_WIN_POS_CENTER );
    gtk_window_set_modal ( GTK_WINDOW ( dialog ), TRUE );
    gtk_window_set_title ( GTK_WINDOW ( dialog ), APPNAME );
    gtk_dialog_set_default_response ( GTK_DIALOG ( dialog ), GTK_RESPONSE_OK );
    gtk_message_dialog_format_secondary_text ( GTK_MESSAGE_DIALOG ( dialog ), "Phrase:" );
    message_area = gtk_message_dialog_get_message_area ( GTK_MESSAGE_DIALOG ( dialog ) );
    entry = gtk_entry_new (  );
    if ( app_context.last_search_phrase )
    {
        gtk_entry_set_text ( GTK_ENTRY ( entry ), app_context.last_search_phrase );
    }

    ignore_whitespaces = gtk_check_button_new_with_label ( "-- Whitespaces" );
    gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON ( ignore_whitespaces ),
        app_context.ignore_whitespaces );
    gtk_widget_show ( ignore_whitespaces );
    gtk_box_pack_end ( GTK_BOX ( message_area ), ignore_whitespaces, TRUE, TRUE, 0 );

    include_field_value = gtk_check_button_new_with_label ( "+ Field Value" );
    gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON ( include_field_value ),
        app_context.include_field_value );
    gtk_widget_show ( include_field_value );
    gtk_box_pack_end ( GTK_BOX ( message_area ), include_field_value, TRUE, TRUE, 0 );

    include_field_name = gtk_check_button_new_with_label ( "+ Field Name" );
    gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON ( include_field_name ),
        app_context.include_field_name );
    gtk_widget_show ( include_field_name );
    gtk_box_pack_end ( GTK_BOX ( message_area ), include_field_name, TRUE, TRUE, 0 );

    include_leaf_name = gtk_check_button_new_with_label ( "+ Leaf Name" );
    gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON ( include_leaf_name ),
        app_context.include_leaf_name );
    gtk_widget_show ( include_leaf_name );
    gtk_box_pack_end ( GTK_BOX ( message_area ), include_leaf_name, TRUE, TRUE, 0 );

    include_holder_name = gtk_check_button_new_with_label ( "+ Holder Name" );
    gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON ( include_holder_name ),
        app_context.include_holder_name );
    gtk_widget_show ( include_holder_name );
    gtk_box_pack_end ( GTK_BOX ( message_area ), include_holder_name, TRUE, TRUE, 0 );

    g_signal_connect ( entry, "activate", G_CALLBACK ( prompt_enter_callback ), dialog );
    gtk_entry_set_width_chars ( GTK_ENTRY ( entry ), 32 );
    gtk_widget_show ( entry );
    gtk_box_pack_end ( GTK_BOX ( message_area ), entry, TRUE, TRUE, 0 );

    response = gtk_dialog_run ( GTK_DIALOG ( dialog ) );
    if ( response == GTK_RESPONSE_OK && ( text = gtk_entry_get_text ( GTK_ENTRY ( entry ) ) ) )
    {
        *options = 0;

        if ( ( app_context.include_holder_name =
                gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON ( include_holder_name ) ) ) )
        {
            *options |= SEARCH_HOLDER_NAME;
        }

        if ( ( app_context.include_leaf_name =
                gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON ( include_leaf_name ) ) ) )
        {
            *options |= SEARCH_LEAF_NAME;
        }

        if ( ( app_context.include_field_name =
                gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON ( include_field_name ) ) ) )
        {
            *options |= SEARCH_FIELD_NAME;
        }

        if ( ( app_context.include_field_value =
                gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON ( include_field_value ) ) ) )
        {
            *options |= SEARCH_FIELD_VALUE;
        }

        if ( ( app_context.ignore_whitespaces =
                gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON ( ignore_whitespaces ) ) ) )
        {
            *options |= SEARCH_IGNORE_WHITESPACES;
        }

        size = strlen ( text ) + 1;
        if ( ( *phrase = ( char * ) malloc ( size ) ) )
        {
            memcpy ( *phrase, text, size );
        }
        app_context.last_search_phrase = *phrase;
    }
    gtk_widget_destroy ( dialog );
    return response == GTK_RESPONSE_OK;
}

static void reset_context ( void )
{
    memset ( &app_context, '\0', sizeof ( app_context ) );
    app_context.include_holder_name = TRUE;
    app_context.include_leaf_name = TRUE;
    app_context.include_field_name = TRUE;
    app_context.include_field_value = TRUE;
    app_context.ignore_whitespaces = TRUE;
}

static int can_create_new_database ( void )
{
    if ( app_context.modified )
    {
        return confirm_inversed ( "Are you sure to DISCARD CHANGES?" );
    }
    return TRUE;
}

static void clear_search_history ( void )
{
    if ( app_context.last_search_phrase )
    {
        g_secure_free_string ( app_context.last_search_phrase );
        app_context.last_search_phrase = NULL;
    }
}

static void forget_database ( void )
{
    clear_search_history (  );
    clear_modified (  );
    app_context.node_selected = NULL;
    app_context.node_parent = NULL;
    free_database (  );
    reset_search (  );
}

static void forget_file ( void )
{
    memset ( app_context.path, '\0', sizeof ( app_context.path ) );
    memset ( app_context.password, '\0', sizeof ( app_context.password ) );
}

static void forget_database_and_file ( void )
{
    forget_database (  );
    forget_file (  );
}

static void app_quit_discard ( void )
{
    forget_database_and_file (  );
    reset_context (  );
    gtk_main_quit (  );
}

static void app_quit ( void )
{
    if ( can_create_new_database (  ) )
    {
        app_quit_discard (  );
    }
}

static gboolean window_on_deleted ( GtkWidget * widget, GdkEvent * event, gpointer data )
{
    UNUSED ( widget );
    UNUSED ( event );
    UNUSED ( data );
    app_quit (  );
    return TRUE;
}

static const char *altername ( const char *name )
{
    if ( !name || !name[0] || name[1] )
    {
        return name;
    }
    switch ( name[0] )
    {
    case 'n':
        return "Name";
    case 'u':
        return "Username";
    case 'p':
        return "Password";
    case 't':
        return "Token";
    case 'd':
        return "Domain";
    case 'h':
        return "Phone";
    case 's':
        return "Secret";
    case 'c':
        return "Address";
    case 'e':
        return "Email";
    case 'r':
        return "Recovery";
    case 'b':
        return "Birthday";
    case 'q':
        return "Question";
    case 'a':
        return "Answer";
    case 'l':
        return "Location";
    case 'x':
        return "Proxy";
    case 'i':
        return "Session";
    default:
        return name;
    }
}

static int new_file ( void )
{
    struct node_t *new_database;
    if ( can_create_new_database (  ) )
    {
        if ( !( new_database = ( struct node_t * ) new_holder ( "Unnamed" ) ) )
        {
            return -1;
        }
        forget_database_and_file (  );
        app_context.database = new_database;
        update_table (  );
        update_tree (  );
        return 0;
    }
    return -1;
}

static gchar *select_open_file ( void )
{
    GtkWidget *dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    gchar *filename = NULL;

    dialog = gtk_file_chooser_dialog_new ( "Open File",
        GTK_WINDOW ( app_context.window ),
        action, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL );

    if ( gtk_dialog_run ( GTK_DIALOG ( dialog ) ) == GTK_RESPONSE_ACCEPT )
    {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER ( dialog );
        filename = gtk_file_chooser_get_filename ( chooser );
    }

    gtk_widget_destroy ( dialog );
    return filename;
}

static gchar *select_save_file ( void )
{
    GtkWidget *dialog;
    GtkFileChooser *chooser;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
    gchar *filename = NULL;
    const char *slash;
    const char *last_filename = "Untitled database.pne";

    dialog = gtk_file_chooser_dialog_new ( "Save File",
        GTK_WINDOW ( app_context.window ),
        action, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL );
    chooser = GTK_FILE_CHOOSER ( dialog );

    gtk_file_chooser_set_do_overwrite_confirmation ( chooser, TRUE );

    if ( app_context.path[0] )
    {
        if ( ( slash = strrchr ( app_context.path, '/' ) ) )
        {
            last_filename = slash + 1;
        } else
        {
            last_filename = app_context.path;
        }
    }

    gtk_file_chooser_set_current_name ( chooser, last_filename );

    if ( gtk_dialog_run ( GTK_DIALOG ( dialog ) ) == GTK_RESPONSE_ACCEPT )
    {
        filename = gtk_file_chooser_get_filename ( chooser );
    }

    gtk_widget_destroy ( dialog );
    return filename;
}

static int open_file ( const char *path, const char *password )
{
    struct node_t *new_database;

    if ( !( new_database = load_database ( path, password ) ) )
    {
        failure ( "Unable to open database" );

    } else
    {
        if ( path != app_context.path )
        {
            strncpy ( app_context.path, path, sizeof ( app_context.path ) - 1 );
        }
        if ( password != app_context.password )
        {
            strncpy ( app_context.password, password, sizeof ( app_context.password ) - 1 );
        }
        forget_database (  );
        app_context.database = new_database;
        update_table (  );
        update_tree (  );
        return TRUE;
    }

    return FALSE;
}

static int open_file_by_path ( const char *path )
{
    int ret = FALSE;
    gchar *password = NULL;

    if ( can_create_new_database (  )
        && prompt_password_allow_empty ( "Enter password", &password ) )
    {
        ret = open_file ( path, password );
    }

    g_secure_free_string ( password );
    return ret;
}

static void setup_encryption_key ( void )
{
    gchar *password = NULL;
    gchar *repassword = NULL;

    if ( prompt_password ( "Change encryption key", &password )
        && prompt_password ( "Confirm encryption key", &repassword ) )
    {
        if ( strcmp ( password, repassword ) )
        {
            failure ( "Inputs are not equal!" );
        } else
        {
            strncpy ( app_context.password, password, sizeof ( app_context.password ) - 1 );
            set_modified (  );
            success ( "Encryption key updated" );
        }
    }

    g_secure_free_string ( password );
    g_secure_free_string ( repassword );
}

static void save_file ( const char *path, struct node_t *branch )
{
    gchar *password = NULL;

    if ( branch == app_context.database )
    {
        if ( !app_context.password[0] )
        {
            setup_encryption_key (  );
            password = app_context.password;
            if ( !password[0] )
            {
                return;
            }
        } else
        {
            password = app_context.password;
        }
    } else if ( !prompt_password_allow_empty ( "Enter password", &password ) )
    {
        return;
    }

    if ( save_database ( path, branch, password ) >= 0 )
    {
        if ( branch == app_context.database )
        {
            clear_modified (  );
            if ( path != app_context.path )
            {
                strncpy ( app_context.path, path, sizeof ( app_context.path ) - 1 );
            }
            if ( password != app_context.password )
            {
                strncpy ( app_context.password, password, sizeof ( app_context.password ) - 1 );
            }
        }
    } else
    {
        if ( branch == app_context.database )
        {
            failure ( "Unable to save database" );
        } else
        {
            failure ( "Unable to export branch" );
        }
    }

    if ( password != app_context.password )
    {
        g_secure_free_string ( password );
    }
}

static void saveas ( struct node_t *node )
{
    gchar *path;

    if ( ( path = select_save_file (  ) ) )
    {
        save_file ( path, node );
        g_secure_free_string ( path );
    }
}

static void create_holder ( int root_holder )
{
    int position;
    char *name = NULL;
    struct holder_t *parent;
    struct holder_t *holder;

    if ( root_holder || !( parent = get_insertion_parent (  ) ) )
    {
        parent = ( struct holder_t * ) app_context.database;
    }

    if ( prompt_text ( "Enter holder name", &name, NULL ) && ( holder = new_holder ( name ) ) )
    {
        if ( append_child_pos ( parent, ( struct node_t * ) holder, &position ) >= 0 )
        {
            set_modified (  );
            reload_tree ( root_holder ? RELOAD_DISCARD_PATH : RELOAD_NORMAL, position );
            focus_tree (  );
        } else
        {
            free_tree ( ( struct node_t * ) holder );
            failure ( "Node already exists" );
        }
    }
    secure_free_string ( name );
}

static void create_leaf ( int root_leaf )
{
    int position;
    char *name = NULL;
    struct holder_t *parent;
    struct leaf_t *leaf;

    if ( root_leaf || !( parent = get_insertion_parent (  ) ) )
    {
        parent = ( struct holder_t * ) app_context.database;
    }

    if ( prompt_text ( "Enter leaf name", &name, NULL ) && ( leaf = new_leaf ( name ) ) )
    {
        if ( append_child_pos ( parent, ( struct node_t * ) leaf, &position ) >= 0 )
        {
            set_modified (  );
            reload_tree ( root_leaf ? RELOAD_DISCARD_PATH : ( leaf_selected (  )? RELOAD_SHRINK_PATH
                    : RELOAD_NORMAL ), position );
            focus_tree (  );
        } else
        {
            free_tree ( ( struct node_t * ) leaf );
            failure ( "Node already exists" );
        }
    }
    secure_free_string ( name );
}

static void fill_search_results ( void )
{
    size_t i;
    const struct search_result_t *ptr;
    GtkTreeModel *model;
    GtkListStore *store;
    GtkTreeIter iter;
    GtkTreeSelection *selection;

    if ( ( selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.tree_view ) ) ) )
    {
        gtk_tree_selection_unselect_all ( selection );
    }

    gtk_tree_view_columns_autosize ( GTK_TREE_VIEW ( app_context.table_view ) );
    model = gtk_tree_view_get_model ( GTK_TREE_VIEW ( app_context.table_view ) );
    store = GTK_LIST_STORE ( model );
    g_object_ref ( model );
    gtk_tree_view_set_model ( GTK_TREE_VIEW ( app_context.table_view ), NULL );
    gtk_list_store_clear ( store );
    for ( i = 0; i < app_context.nresults; i++ )
    {
        ptr = app_context.results + i;
        gtk_list_store_append ( store, &iter );
        gtk_list_store_set ( store, &iter, TABLE_NAME_COLUMN, ptr->name, TABLE_VALUE_COLUMN,
            ptr->value ? get_masked_value ( ptr->name, ptr->value ) : "", -1 );
    }
    gtk_tree_view_set_model ( GTK_TREE_VIEW ( app_context.table_view ), model );
    g_object_unref ( model );
    app_context.node_selected = NULL;
}

static void menu_new_file ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );
    new_file (  );
}

static void menu_open_file ( GtkMenuItem * menu_item, gpointer data )
{
    gchar *path = NULL;
    UNUSED ( menu_item );
    UNUSED ( data );

    if ( ( path = select_open_file (  ) ) )
    {
        open_file_by_path ( path );
        g_secure_free_string ( path );
    }
}

static void menu_reload_file ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    if ( app_context.path[0] && can_create_new_database (  ) )
    {
        open_file ( app_context.path, app_context.password );
    }
}

static void menu_clear_file ( GtkMenuItem * menu_item, gpointer data )
{
    struct node_t *new_database;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( can_create_new_database (  ) )
    {
        if ( !( new_database = ( struct node_t * ) new_holder ( "Unnamed" ) ) )
        {
            return;
        }
        forget_database (  );
        app_context.database = new_database;
        update_table (  );
        update_tree (  );
    }
}

static void menu_save_file ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    if ( !app_context.path[0] )
    {
        saveas ( app_context.database );
    } else
    {
        save_file ( app_context.path, app_context.database );
    }
}

static void menu_saveas_file ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    saveas ( app_context.database );
}

static void menu_set_encryption_key ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );
    setup_encryption_key (  );
}

static void onmerged ( struct database_stats_t *stats )
{
    if ( !stats->holders_added && !stats->leaves_added && !stats->fields_added
        && !stats->fields_updated )
    {
        infobox ( "Statistics", "Nothing to update" );
    } else
    {
        set_modified (  );
        reload_tree ( RELOAD_NORMAL, -1 );
        focus_tree (  );
        show_database_stats ( stats, TRUE );
    }
}

static void menu_quit ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );
    app_quit (  );
}

static void menu_search_root ( GtkMenuItem * menu_item, gpointer data )
{
    int options = 0;
    gchar *phrase = NULL;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( search_prompt ( &options, "Search Root", &phrase ) )
    {
        reset_search (  );
        app_context.search_base_length = 0;
        if ( search_run ( app_context.database, options, phrase, &app_context.results,
                &app_context.nresults ) < 0 )
        {
            failure ( "Search failed" );
            return;
        }

        fill_search_results (  );
    }
}

static int set_search_base_to_selected ( void )
{
    int i;
    gint depth;
    gint *indices;
    GList *temp;
    GList *selected_rows;
    GtkTreePath *path;
    GtkTreeSelection *selection;
    GtkTreeModel *model = NULL;

    if ( !( selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.tree_view ) ) ) )
    {
        return -1;
    }

    selected_rows = gtk_tree_selection_get_selected_rows ( selection, &model );
    if ( !selected_rows || !model )
    {
        return -1;
    }

    temp = selected_rows;
    if ( !temp->next )
    {
        path = temp->data;
        if ( ( indices = gtk_tree_path_get_indices ( path ) ) )
        {
            depth = gtk_tree_path_get_depth ( path );
            app_context.search_base_length = 0;
            for ( i = 0;
                i < depth
                && i < ( int ) ( sizeof ( app_context.search_base_indices ) / sizeof ( int ) );
                i++ )
            {
                app_context.search_base_indices[app_context.search_base_length++] = indices[i];
            }
        }
    }
    g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
    return 0;
}

static void menu_search_branch ( GtkMenuItem * menu_item, gpointer data )
{
    int options = 0;
    gchar *phrase = NULL;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( app_context.node_selected && search_prompt ( &options, "Search Branch", &phrase ) )
    {
        reset_search (  );
        if ( set_search_base_to_selected (  ) < 0 )
        {
            return;
        }
        if ( app_context.node_selected
            && search_run ( app_context.node_selected, options, phrase, &app_context.results,
                &app_context.nresults ) < 0 )
        {
            failure ( "Search failed" );
            return;
        }

        fill_search_results (  );
    }
}

static void menu_expand_branch ( GtkMenuItem * menu_item, gpointer data )
{
    GList *selected_rows = NULL;
    GtkTreePath *path;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( ( path = tree_get_selected_path ( &selected_rows ) ) )
    {
        gtk_tree_view_expand_row ( GTK_TREE_VIEW ( app_context.tree_view ), path, TRUE );
    }

    if ( selected_rows )
    {
        g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
    }
}

static void menu_full_sort ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    sort_tree ( app_context.database );
    set_modified (  );
    update_tree (  );
    success ( "Sorted successfully" );
}

static void menu_new_holder ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    create_holder ( FALSE );
}

static void menu_new_leaf ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    create_leaf ( FALSE );
}

static void menu_root_holder ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    create_holder ( TRUE );
}

static void menu_root_leaf ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    create_leaf ( TRUE );
}

static void menu_rename_root ( GtkMenuItem * menu_item, gpointer data )
{
    gchar *name = NULL;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( prompt_text ( "Enter root name", &name, app_context.database->name ) )
    {
        if ( rename_node ( NULL, app_context.database, name, NULL ) >= 0 )
        {
            set_modified (  );
            update_table (  );
            reload_tree ( RELOAD_DISCARD_PATH, -1 );
        } else
        {
            failure ( "Node already exists" );
        }
        secure_free_string ( name );
    }
}

static void menu_rename_node ( GtkMenuItem * menu_item, gpointer data )
{
    int position;
    gchar *name = NULL;
    struct holder_t *parent;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( !( parent = get_node_parent (  ) ) )
    {
        parent = ( struct holder_t * ) app_context.database;
    }

    if ( app_context.node_selected
        && prompt_text ( "Enter node name", &name, app_context.node_selected->name ) )
    {
        if ( rename_node ( parent, app_context.node_selected, name, &position ) >= 0 )
        {
            set_modified (  );
            reload_tree ( RELOAD_SHRINK_PATH, position );
            focus_tree (  );
        } else
        {
            failure ( "Node already exists" );
        }
        secure_free_string ( name );
    }
}

static void menu_delete_node ( GtkMenuItem * menu_item, gpointer data )
{
    struct holder_t *holder;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( app_context.node_selected
        && app_context.node_parent
        && !app_context.node_parent->is_leaf
        && app_context.node_selected != app_context.node_parent
        && confirm ( "Are you sure to delete node?" ) )
    {
        if ( !app_context.node_selected->is_leaf )
        {
            holder = ( struct holder_t * ) app_context.node_selected;
            if ( holder->children_head )
            {
                warning ( "Holder is NOT empty!" );
                if ( !confirm ( "Are you sure to proceed?" ) )
                {
                    return;
                }
            }
        }
        delete_child ( ( struct holder_t * ) app_context.node_parent, app_context.node_selected );
        set_modified (  );
        app_context.node_selected = NULL;
        app_context.node_parent = NULL;
        update_table (  );
        reload_tree ( RELOAD_SHRINK_PATH, -1 );
        focus_tree (  );
    }
}

static void menu_import_branch ( GtkMenuItem * menu_item, gpointer data )
{
    int position;
    gchar *path = NULL;
    gchar *password = NULL;
    struct holder_t *parent;
    struct node_t *branch;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( confirm_inversed ( "Import as root child?" ) )
    {
        parent = ( struct holder_t * ) app_context.database;
    } else
    {
        if ( !app_context.node_selected || app_context.node_selected->is_leaf )
        {
            return;
        }
        parent = ( struct holder_t * ) app_context.node_selected;
    }

    if ( ( path = select_open_file (  ) )
        && prompt_password_allow_empty ( "Enter password", &password ) )
    {
        if ( ( branch = load_database ( path, password ) ) )
        {
            if ( append_child_pos ( parent, branch, &position ) >= 0 )
            {
                set_modified (  );
                reload_tree ( ( struct node_t * ) parent == app_context.database
                    ? RELOAD_DISCARD_PATH : RELOAD_NORMAL, position );
                focus_tree (  );
            } else
            {
                free_tree ( branch );
                failure ( "Node already exists" );
            }
        } else
        {
            failure ( "Unable to import branch" );
        }
    }

    g_secure_free_string ( path );
    g_secure_free_string ( password );
}

static void menu_merge_branch ( GtkMenuItem * menu_item, gpointer data )
{
    gchar *path = NULL;
    gchar *password = NULL;
    struct node_t *node;
    struct node_t *branch;
    struct database_stats_t stats = { 0 };

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( confirm ( "Merge with tree root?" ) )
    {
        node = app_context.database;
    } else
    {
        if ( !app_context.node_selected )
        {
            return;
        }
        node = app_context.node_selected;
    }

    if ( ( path = select_open_file (  ) )
        && prompt_password_allow_empty ( "Enter password", &password ) )
    {
        if ( ( branch = load_database ( path, password ) ) )
        {
            if ( merge_tree ( node, branch, &stats ) >= 0 )
            {
                onmerged ( &stats );
            } else
            {
                failure ( "Unable to merge branch" );
            }
        } else
        {
            failure ( "Unable to load branch" );
        }
    }

    g_secure_free_string ( path );
    g_secure_free_string ( password );
}

static void menu_export_branch ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    if ( app_context.node_selected )
    {
        saveas ( app_context.node_selected );
    }
}

static int menu_copy_path_internal ( void )
{
    int i;
    int depth;
    int error = FALSE;
    gint *indices;
    GList *temp;
    GList *selected_rows;
    GtkTreePath *path;
    GtkTreeSelection *selection;
    GtkTreeModel *model = NULL;
    char strpath[PATH_SIZE];
    struct node_t *node;
    struct field_t *field;
    GtkClipboard *clipboard;

    strpath[0] = '\0';

    if ( !( selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.tree_view ) ) ) )
    {
        return FALSE;
    }

    selected_rows = gtk_tree_selection_get_selected_rows ( selection, &model );
    if ( !selected_rows || !model )
    {
        return FALSE;
    }

    temp = selected_rows;
    if ( !temp->next )
    {
        path = temp->data;
        if ( ( indices = gtk_tree_path_get_indices ( path ) ) )
        {
            depth = gtk_tree_path_get_depth ( path );
            node = app_context.database;

            for ( i = 0; i < depth; i++ )
            {
                if ( node->is_leaf )
                {
                    error = TRUE;
                    break;
                }

                if ( !( node = get_nth_node ( ( struct holder_t * ) node, indices[i] ) ) )
                {
                    error = TRUE;
                    break;
                }

                snprintf ( strpath + strlen ( strpath ), sizeof ( strpath ) - strlen ( strpath ),
                    "%s", node->name );

                if ( i + 1 < depth )
                {
                    snprintf ( strpath + strlen ( strpath ),
                        sizeof ( strpath ) - strlen ( strpath ), " " );
                }
            }
        }
    }
    g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
    if ( error )
    {
        return FALSE;
    }

    if ( leaf_selected (  )
        && ( selection =
            gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.table_view ) ) ) )
    {
        selected_rows = gtk_tree_selection_get_selected_rows ( selection, &model );
        if ( selected_rows && model )
        {
            temp = selected_rows;
            if ( !temp->next )
            {
                path = temp->data;
                if ( ( indices = gtk_tree_path_get_indices ( path ) )
                    && gtk_tree_path_get_depth ( path ) == 1
                    && ( field =
                        get_nth_field ( ( struct leaf_t * ) app_context.node_selected,
                            indices[0] ) ) )
                {
                    snprintf ( strpath + strlen ( strpath ),
                        sizeof ( strpath ) - strlen ( strpath ), "%s%s", strpath[0] ? " " : "",
                        field->name );
                }
            }
            g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
        }
    }

    if ( ( clipboard = gtk_clipboard_get ( GDK_SELECTION_CLIPBOARD ) ) )
    {
        gtk_clipboard_set_text ( clipboard, strpath, -1 );
    }

    return TRUE;
}

static void menu_copy_path ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    if ( !menu_copy_path_internal (  ) )
    {
        failure ( "Path not selected!" );
    }
}

static int menu_as_address_internal ( void )
{
    int i;
    int depth;
    int error = FALSE;
    gint *indices;
    GList *temp;
    GList *selected_rows;
    GtkTreePath *path;
    GtkTreeSelection *selection;
    GtkTreeModel *model = NULL;
    char strpath[PATH_SIZE];
    char postcode[16];
    char street[128];
    char housenumber[16];
    char fulladdress[2 * PATH_SIZE];
    struct node_t *node;
    struct field_t *field;
    GtkClipboard *clipboard;

    strpath[0] = '\0';
    street[0] = '\0';
    housenumber[0] = '\0';

    if ( leaf_selected (  )
        && ( selection =
            gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.table_view ) ) ) )
    {
        selected_rows = gtk_tree_selection_get_selected_rows ( selection, &model );
        if ( selected_rows && model )
        {
            temp = selected_rows;
            if ( !temp->next )
            {
                path = temp->data;
                if ( ( indices = gtk_tree_path_get_indices ( path ) )
                    && gtk_tree_path_get_depth ( path ) == 1
                    && ( field =
                        get_nth_field ( ( struct leaf_t * ) app_context.node_selected,
                            indices[0] ) ) )
                {
                    strncpy ( housenumber, field->name, sizeof ( housenumber ) - 1 );
                }
            }
            g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
        }
    }

    if ( !( selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW ( app_context.tree_view ) ) ) )
    {
        return FALSE;
    }

    selected_rows = gtk_tree_selection_get_selected_rows ( selection, &model );
    if ( !selected_rows || !model )
    {
        return FALSE;
    }

    temp = selected_rows;
    if ( !temp->next )
    {
        path = temp->data;
        if ( ( indices = gtk_tree_path_get_indices ( path ) ) )
        {
            depth = gtk_tree_path_get_depth ( path );
            node = app_context.database;

            for ( i = 0; i < depth; i++ )
            {
                if ( node->is_leaf )
                {
                    error = TRUE;
                    break;
                }

                if ( !( node = get_nth_node ( ( struct holder_t * ) node, indices[i] ) ) )
                {
                    error = TRUE;
                    break;
                }

                if ( i + 2 < depth )
                {
                    snprintf ( strpath + strlen ( strpath ),
                        sizeof ( strpath ) - strlen ( strpath ), "%s", node->name );

                    if ( i + 3 < depth )
                    {
                        snprintf ( strpath + strlen ( strpath ),
                            sizeof ( strpath ) - strlen ( strpath ), " " );
                    }
                } else if ( i + 1 < depth )
                {
                    strncpy ( postcode, node->name, sizeof ( postcode ) - 1 );
                } else
                {
                    strncpy ( street, node->name, sizeof ( street ) - 1 );
                }
            }
        }
    }
    g_list_free_full ( selected_rows, ( GDestroyNotify ) gtk_tree_path_free );
    if ( error )
    {
        return FALSE;
    }

    snprintf ( fulladdress, sizeof ( fulladdress ), "%s %s %s %s", street, housenumber, postcode,
        strpath );

    if ( ( clipboard = gtk_clipboard_get ( GDK_SELECTION_CLIPBOARD ) ) )
    {
        gtk_clipboard_set_text ( clipboard, fulladdress, -1 );
    }

    return TRUE;
}

static void menu_as_address ( GtkMenuItem * menu_item, gpointer data )
{
    UNUSED ( menu_item );
    UNUSED ( data );

    if ( !menu_as_address_internal (  ) )
    {
        failure ( "Path not selected!" );
    }
}

static void menu_new_field ( GtkMenuItem * menu_item, gpointer data )
{
    int position;
    char *name = NULL;
    char *value = NULL;
    struct field_t *field;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( leaf_selected (  ) )
    {
        if ( prompt_text ( "Enter field name", &name, NULL )
            && prompt_text ( "Enter field value", &value, NULL )
            && ( field = new_field ( altername ( name ), value ) ) )
        {
            if ( append_field_pos ( ( struct leaf_t * ) app_context.node_selected, field,
                    &position ) >= 0 )
            {
                set_modified (  );
                reload_table ( position );
                focus_table (  );
            } else
            {
                free_field ( field );
                failure ( "Field already exists" );
            }
        }
        secure_free_string ( name );
        secure_free_string ( value );
    }
}

static void menu_rename_field ( GtkMenuItem * menu_item, gpointer data )
{
    int position;
    char *name = NULL;
    struct field_t *field;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( leaf_selected (  ) && ( field = table_get_field_selected (  ) )
        && prompt_text ( "Enter field name", &name, field->name ) )
    {
        if ( rename_field ( ( struct leaf_t * ) app_context.node_selected, field,
                altername ( name ), &position ) >= 0 )
        {
            set_modified (  );
            reload_table ( position );
        } else
        {
            failure ( "Field already exists" );
        }
        secure_free_string ( name );
    }
}

static void menu_edit_field ( GtkMenuItem * menu_item, gpointer data )
{
    char *value = NULL;
    struct field_t *field;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( ( field = table_get_field_selected (  ) )
        && prompt_text ( "Enter field value", &value, field->value ) )
    {
        if ( edit_field ( field, value ) >= 0 )
        {
            set_modified (  );
            reload_table ( -1 );
        } else
        {
            failure ( "Field edit failed" );
        }
        secure_free_string ( value );
    }
}

static void menu_view_field ( GtkMenuItem * menu_item, gpointer data )
{
    struct field_t *field;
    struct timeval tv = { 0 };
    char title[512];
    char time[32];
    char content[64];

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( ( field = table_get_field_selected (  ) ) )
    {
        snprintf ( title, sizeof ( title ), "\n%s = %s", field->name, field->value );
        tv.tv_sec = field->modified;
        if ( format_time ( &tv, time, sizeof ( time ) ) >= 0 )
        {
            snprintf ( content, sizeof ( content ), "Modified: %s\n", time );
        } else
        {
            content[0] = '\0';
        }
        infobox ( title, content );
        memset ( title, '\0', sizeof ( title ) );
        memset ( content, '\0', sizeof ( content ) );
        memset ( time, '\0', sizeof ( time ) );
        memset ( &tv, '\0', sizeof ( tv ) );
    }
}

static void menu_delete_field ( GtkMenuItem * menu_item, gpointer data )
{
    struct field_t *field;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( ( field = table_get_field_selected (  ) ) && confirm ( "Are you sure to delete field?" ) )
    {
        delete_field ( ( struct leaf_t * ) app_context.node_selected, field );
        set_modified (  );
        reload_table ( -1 );
    }
}

static void menu_copy_field_value ( GtkMenuItem * menu_item, gpointer data )
{
    int position;
    struct field_t *field;
    GtkClipboard *clipboard;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( ( clipboard = gtk_clipboard_get ( GDK_SELECTION_CLIPBOARD ) ) )
    {
        if ( ( field = table_get_field_selected (  ) ) )
        {
            gtk_clipboard_set_text ( clipboard, field->value, -1 );
            focus_table (  );
            return;
        }

        position = table_get_position_selected (  );

        if ( position >= 0 && position < ( ssize_t ) app_context.nresults
            && app_context.results[position].value )
        {
            gtk_clipboard_set_text ( clipboard, app_context.results[position].value, -1 );
            focus_table (  );
            return;
        }
    }
    failure ( "Value not present!" );
}

static void menu_paste_field_value ( GtkMenuItem * menu_item, gpointer data )
{
    struct field_t *field;
    GtkClipboard *clipboard;
    gchar *value;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( ( field = table_get_field_selected (  ) )
        && ( clipboard = gtk_clipboard_get ( GDK_SELECTION_CLIPBOARD ) ) )
    {
        if ( ( value = gtk_clipboard_wait_for_text ( clipboard ) ) )
        {
            if ( confirm ( "Are you sure to change field value?" ) )
            {
                if ( edit_field ( field, value ) >= 0 )
                {
                    set_modified (  );
                    reload_table ( -1 );
                    focus_table (  );
                } else
                {
                    failure ( "Field edit failed" );
                }
            }
            g_secure_free_string ( value );
        }
    }
}

static void menu_copy_as_tsv ( GtkMenuItem * menu_item, gpointer data )
{
    char *tsv;
    GtkClipboard *clipboard;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( leaf_selected (  ) && ( clipboard = gtk_clipboard_get ( GDK_SELECTION_CLIPBOARD ) ) )
    {
        if ( ( tsv = copy_as_tsv ( ( struct leaf_t * ) app_context.node_selected ) ) )
        {
            gtk_clipboard_set_text ( clipboard, tsv, -1 );
            secure_free_string ( tsv );
            focus_table (  );
        }
    }
}

static void menu_paste_as_tsv ( GtkMenuItem * menu_item, gpointer data )
{
    char *tsv;
    struct database_stats_t stats = { 0 };
    GtkClipboard *clipboard;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( leaf_selected (  )
        && confirm ( "Are you sure to paste as TSV?" )
        && ( clipboard = gtk_clipboard_get ( GDK_SELECTION_CLIPBOARD ) ) )
    {
        if ( ( tsv = gtk_clipboard_wait_for_text ( clipboard ) ) )
        {
            paste_as_tsv ( ( struct leaf_t * ) app_context.node_selected, tsv, &stats );
            g_free ( tsv );
            reload_table ( -1 );
            focus_table (  );
            show_database_stats ( &stats, FALSE );
        }
    }
}

#ifdef ENABLE_TOPT
extern int topt ( const char *secret, char *code, size_t size );

static void menu_generate_topt ( GtkMenuItem * menu_item, gpointer data )
{
    size_t i;
    size_t n;
    size_t len;
    struct field_t *field;
    GtkClipboard *clipboard;
    char code[32];
    char title[64];
    char secret[256];

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( ( field = table_get_field_selected (  ) ) )
    {
        len = strlen ( field->value );
        for ( i = 0, n = 0; i < len && n + 1 < sizeof ( secret ); i++ )
        {
            if ( !isblank ( field->value[i] ) )
            {
                secret[n++] = field->value[i];
            }
        }
        secret[n] = '\0';

        if ( topt ( secret, code, sizeof ( code ) ) >= 0 && strlen ( code ) == 6 )
        {
            snprintf ( title, sizeof ( title ), "\n* %c %c %c %c %c %c *",
                code[0], code[1], code[2], code[3], code[4], code[5] );

            if ( ( clipboard = gtk_clipboard_get ( GDK_SELECTION_CLIPBOARD ) )
                && table_get_field_selected (  ) )
            {
                gtk_clipboard_set_text ( clipboard, code, -1 );
                infobox ( title, "Saved to clipboard" );
            }
        }
        memset ( secret, '\0', sizeof ( secret ) );
        memset ( code, '\0', sizeof ( code ) );
    }
}
#endif

static void menu_put_current_time ( GtkMenuItem * menu_item, gpointer data )
{
    int position;
    char *name = NULL;
    char *value = NULL;
    char timestr[64] = { 0 };
    struct field_t *field;
    time_t time_sec;
    struct tm time_struct;

    UNUSED ( menu_item );
    UNUSED ( data );

    time_sec = time ( NULL );

    if ( !localtime_r ( &time_sec, &time_struct ) )
    {
        return;
    }

    strftime ( timestr, sizeof ( timestr ), "%d-%m-%Y %H:%M:%S", &time_struct );

    if ( leaf_selected (  ) )
    {
        if ( prompt_text ( "Name the moment", &name, NULL )
            && ( field = new_field ( altername ( name ), timestr ) ) )
        {
            if ( append_field_pos ( ( struct leaf_t * ) app_context.node_selected, field,
                    &position ) >= 0 )
            {
                set_modified (  );
                reload_table ( position );
                focus_table (  );
            } else
            {
                failure ( "Field already exists" );
            }
        }
        secure_free_string ( name );
        secure_free_string ( value );
        memset ( timestr, '\0', sizeof ( timestr ) );
    }
}

static void menu_generate_password ( GtkMenuItem * menu_item, gpointer data )
{
    int position;
    char *name = NULL;
    char *value = NULL;
    char password[PASSWORD_SIZE];
    struct field_t *field;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( leaf_selected (  ) )
    {
        if ( generate_password ( password, sizeof ( password ) ) >= 0 )
        {
            if ( prompt_text ( "Name the password", &name, NULL )
                && ( field = new_field ( altername ( name ), password ) ) )
            {
                if ( append_field_pos ( ( struct leaf_t * ) app_context.node_selected, field,
                        &position ) >= 0 )
                {
                    set_modified (  );
                    reload_table ( position );
                    focus_table (  );
                } else
                {
                    failure ( "Field already exists" );
                }
            }
            secure_free_string ( name );
            secure_free_string ( value );
        }
        memset ( password, '\0', sizeof ( password ) );
    }
}

static void menu_random_path ( GtkMenuItem * menu_item, gpointer data )
{
    int fd;
    int n;
    int index;
    int depth = 0;
    int indices[PATH_SIZE];
    struct node_t *node;
    struct holder_t *holder;
    struct leaf_t *leaf;
    struct field_t *field;

    UNUSED ( menu_item );
    UNUSED ( data );

    if ( !( holder = ( struct holder_t * ) app_context.database ) )
    {
        return;
    }

    if ( ( fd = open ( "/dev/urandom", O_RDONLY ) ) < 0 )
    {
        return;
    }

    while ( !holder->is_leaf )
    {
        if ( depth >= PATH_SIZE )
        {
            close ( fd );
            return;
        }

        if ( read ( fd, &index, sizeof ( index ) ) < 0 )
        {
            close ( fd );
            return;
        }

        if ( index < 0 )
        {
            index = -index;
        }

        for ( node = holder->children_head, n = 0; node; node = node->next )
        {
            n++;
        }

        if ( !n )
        {
            break;
        }

        index %= n;
        indices[depth++] = index;
        for ( node = holder->children_head, n = 0; node; node = node->next, n++ )
        {
            if ( n == index )
            {
                holder = ( struct holder_t * ) node;
            }
        }
    }

    select_tree_path ( indices, depth, RELOAD_NORMAL, -1 );

    if ( holder->is_leaf )
    {
        leaf = ( struct leaf_t * ) holder;
        if ( read ( fd, &index, sizeof ( index ) ) < 0 )
        {
            close ( fd );
            return;
        }

        if ( index < 0 )
        {
            index = -index;
        }

        for ( field = leaf->fields_head, n = 0; field; field = field->next )
        {
            n++;
        }

        if ( !n )
        {
            close ( fd );
            return;
        }

        reload_table ( index % n );
    }

    close ( fd );
}

static GdkPixbuf *load_app_icon ( void )
{
    size_t size;
    GdkPixbufLoader *loader;

    loader = gdk_pixbuf_loader_new_with_mime_type ( "image/png", NULL );
    size = &_binary_res_app_icon_png_end - &_binary_res_app_icon_png_start;
    gdk_pixbuf_loader_write ( loader, &_binary_res_app_icon_png_start, size, NULL );
    return gdk_pixbuf_loader_get_pixbuf ( loader );
}

static void menu_about ( GtkMenuItem * menu_item, gpointer data )
{
    GdkPixbuf *pixbuf;
    GtkWidget *dialog;

    UNUSED ( menu_item );
    UNUSED ( data );

    dialog = gtk_about_dialog_new (  );
    gtk_about_dialog_set_program_name ( GTK_ABOUT_DIALOG ( dialog ), APPNAME );
    gtk_about_dialog_set_version ( GTK_ABOUT_DIALOG ( dialog ), APPVER );
    gtk_about_dialog_set_comments ( GTK_ABOUT_DIALOG ( dialog ), APPDESC );
    pixbuf = load_app_icon (  );
    gtk_about_dialog_set_logo ( GTK_ABOUT_DIALOG ( dialog ), pixbuf );
    g_object_unref ( pixbuf );
    gtk_dialog_run ( GTK_DIALOG ( dialog ) );
    gtk_widget_destroy ( dialog );
}

static void add_menu_item ( GtkWidget * submenu, const char *name, GCallback callback,
    GtkAccelGroup * accel_group, gint shortcut, gint mask )
{
    GtkWidget *menu_item;
    menu_item = gtk_menu_item_new_with_label ( name );
    g_signal_connect ( menu_item, "activate", callback, NULL );
    gtk_widget_add_accelerator ( menu_item, "activate", accel_group, shortcut, mask,
        GTK_ACCEL_VISIBLE );
    gtk_menu_shell_append ( GTK_MENU_SHELL ( submenu ), menu_item );
}

static void app_lock ( void )
{
    if ( app_context.window )
    {
        gtk_widget_hide ( app_context.rootbox );
        gtk_widget_show ( app_context.authbox );
        gtk_widget_grab_focus ( app_context.passbox );
        app_context.shortpass = 7;
    }
}

static void app_unlock ( void )
{
    if ( app_context.window )
    {
        gtk_entry_set_text ( GTK_ENTRY ( app_context.passbox ), "" );
        gtk_widget_show ( app_context.rootbox );
        gtk_widget_hide ( app_context.authbox );
    }
}

static gboolean app_lock_handler ( gpointer user_data )
{
    UNUSED ( user_data );
    if ( app_context.password[0] )
    {
        app_lock (  );
    }
    return TRUE;
}

static void auth_check ( void )
{
    const gchar *password = NULL;

    if ( app_context.window
        && ( password = gtk_entry_get_text ( GTK_ENTRY ( app_context.passbox ) ) ) )
    {
        if ( !strcmp ( password, app_context.password ) )
        {
            app_unlock (  );
        } else if ( app_context.shortpass >= 0 && strlen ( password ) >= 3
            && strlen ( app_context.password ) >= 3
            && tolower ( password[0] ) == tolower ( app_context.password[0] )
            && tolower ( password[1] ) == tolower ( app_context.password[1] )
            && tolower ( password[2] ) == tolower ( app_context.password[2] ) )
        {
            app_unlock (  );
        }
    }

    app_context.shortpass--;
}

static gboolean auth_on_key_press ( GtkWidget * widget, GdkEventKey * event, gpointer user_data )
{
    UNUSED ( widget );
    UNUSED ( event );
    UNUSED ( user_data );
    auth_check (  );
    return FALSE;
}

static void show_usage ( void )
{
    fprintf ( stderr, "usage: passnote [database]\n" );
}

void label_set_fontsize ( GtkWidget * label, int fontsize )
{
    PangoAttrList *attrlist;
    PangoAttribute *attr;
    attrlist = pango_attr_list_new (  );
    attr = pango_attr_size_new_absolute ( fontsize * PANGO_SCALE );
    pango_attr_list_insert ( attrlist, attr );
    gtk_label_set_attributes ( GTK_LABEL ( label ), attrlist );
    pango_attr_list_unref ( attrlist );
}

int main ( int argc, char *argv[] )
{
    GtkWidget *menu_bar;
    GtkWidget *file_menu;
    GtkWidget *file_submenu;
    GtkWidget *tree_menu;
    GtkWidget *tree_submenu;
    GtkWidget *field_menu;
    GtkWidget *field_submenu;
    GtkWidget *help_menu;
    GtkWidget *help_submenu;
    GtkWidget *hbox;
    GtkWidget *tree_scrolled_window;
    GtkWidget *table_scrolled_window;
    GtkAccelGroup *accel_group;
    GdkPixbuf *pixbuf;
    GtkWidget *mainbox;
    GtkWidget *entrybox;
    GtkWidget *authlabel;

    if ( argc >= 2 && ( !strcmp ( argv[1], "-h" ) || !strcmp ( argv[1], "--help" ) ) )
    {
        show_usage (  );
        return 0;
    }

    reset_context (  );
    gtk_init ( 0, NULL );
    signal ( SIGINT, SIG_IGN );
    signal ( SIGTERM, SIG_IGN );

    app_context.window = gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title ( GTK_WINDOW ( app_context.window ), APPNAME );
    gtk_window_maximize ( GTK_WINDOW ( app_context.window ) );
    pixbuf = load_app_icon (  );
    gtk_window_set_icon ( GTK_WINDOW ( app_context.window ), pixbuf );
    g_object_unref ( pixbuf );
    menu_bar = gtk_menu_bar_new (  );
    accel_group = gtk_accel_group_new (  );
    gtk_window_add_accel_group ( GTK_WINDOW ( app_context.window ), accel_group );

    /* File */
    file_menu = gtk_menu_item_new_with_label ( "File" );
    file_submenu = gtk_menu_new (  );

    add_menu_item ( file_submenu, "New", G_CALLBACK ( menu_new_file ),
        accel_group, GDK_b, GDK_CONTROL_MASK );
    add_menu_item ( file_submenu, "Open", G_CALLBACK ( menu_open_file ),
        accel_group, GDK_o, GDK_CONTROL_MASK );
    add_menu_item ( file_submenu, "Reload", G_CALLBACK ( menu_reload_file ),
        accel_group, GDK_F5, 0 );
    add_menu_item ( file_submenu, "Clear", G_CALLBACK ( menu_clear_file ),
        accel_group, GDK_F10, 0 );
    add_menu_item ( file_submenu, "Save", G_CALLBACK ( menu_save_file ),
        accel_group, GDK_s, GDK_CONTROL_MASK );
    add_menu_item ( file_submenu, "Save as", G_CALLBACK ( menu_saveas_file ),
        accel_group, GDK_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( file_submenu, "Encryption", G_CALLBACK ( menu_set_encryption_key ),
        accel_group, GDK_y, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( file_submenu, "Quit", G_CALLBACK ( menu_quit ),
        accel_group, GDK_q, GDK_CONTROL_MASK );

    gtk_menu_item_set_submenu ( GTK_MENU_ITEM ( file_menu ), file_submenu );
    gtk_menu_shell_append ( GTK_MENU_SHELL ( menu_bar ), file_menu );

    /* Tree */
    tree_menu = gtk_menu_item_new_with_label ( "Tree" );
    tree_submenu = gtk_menu_new (  );

    add_menu_item ( tree_submenu, "Search Root", G_CALLBACK ( menu_search_root ),
        accel_group, GDK_f, GDK_CONTROL_MASK );
    add_menu_item ( tree_submenu, "Search Branch", G_CALLBACK ( menu_search_branch ),
        accel_group, GDK_f, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Expand Branch", G_CALLBACK ( menu_expand_branch ),
        accel_group, GDK_w, GDK_CONTROL_MASK );
    add_menu_item ( tree_submenu, "Full Sort", G_CALLBACK ( menu_full_sort ),
        accel_group, GDK_F7, 0 );
    add_menu_item ( tree_submenu, "New Holder", G_CALLBACK ( menu_new_holder ),
        accel_group, GDK_h, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "New Leaf", G_CALLBACK ( menu_new_leaf ),
        accel_group, GDK_l, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Root Holder", G_CALLBACK ( menu_root_holder ),
        accel_group, GDK_u, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Root Leaf", G_CALLBACK ( menu_root_leaf ),
        accel_group, GDK_p, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Rename Root", G_CALLBACK ( menu_rename_root ),
        accel_group, GDK_t, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Rename Node", G_CALLBACK ( menu_rename_node ),
        accel_group, GDK_r, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Delete Node", G_CALLBACK ( menu_delete_node ),
        accel_group, GDK_d, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Merge Branch", G_CALLBACK ( menu_merge_branch ),
        accel_group, GDK_m, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Import Branch", G_CALLBACK ( menu_import_branch ),
        accel_group, GDK_i, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Export Branch", G_CALLBACK ( menu_export_branch ),
        accel_group, GDK_e, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Selection Path", G_CALLBACK ( menu_copy_path ),
        accel_group, GDK_g, GDK_CONTROL_MASK | GDK_SHIFT_MASK );
    add_menu_item ( tree_submenu, "Selection Address", G_CALLBACK ( menu_as_address ),
        accel_group, GDK_a, GDK_CONTROL_MASK | GDK_SHIFT_MASK );

    gtk_menu_item_set_submenu ( GTK_MENU_ITEM ( tree_menu ), tree_submenu );
    gtk_menu_shell_append ( GTK_MENU_SHELL ( menu_bar ), tree_menu );

    /* Field */
    field_menu = gtk_menu_item_new_with_label ( "Field" );
    field_submenu = gtk_menu_new (  );

    add_menu_item ( field_submenu, "New", G_CALLBACK ( menu_new_field ),
        accel_group, GDK_n, GDK_CONTROL_MASK );
    add_menu_item ( field_submenu, "Rename", G_CALLBACK ( menu_rename_field ),
        accel_group, GDK_r, GDK_CONTROL_MASK );
    add_menu_item ( field_submenu, "Edit", G_CALLBACK ( menu_edit_field ),
        accel_group, GDK_Return, GDK_SHIFT_MASK );
    add_menu_item ( field_submenu, "View", G_CALLBACK ( menu_view_field ),
        accel_group, GDK_Return, 0 );
    add_menu_item ( field_submenu, "Delete", G_CALLBACK ( menu_delete_field ),
        accel_group, GDK_d, GDK_CONTROL_MASK );
    add_menu_item ( field_submenu, "Copy value", G_CALLBACK ( menu_copy_field_value ),
        accel_group, GDK_c, GDK_CONTROL_MASK );
    add_menu_item ( field_submenu, "Paste value", G_CALLBACK ( menu_paste_field_value ),
        accel_group, GDK_v, GDK_CONTROL_MASK );
    add_menu_item ( field_submenu, "Copy as TSV", G_CALLBACK ( menu_copy_as_tsv ),
        accel_group, GDK_j, GDK_CONTROL_MASK );
    add_menu_item ( field_submenu, "Paste as TSV", G_CALLBACK ( menu_paste_as_tsv ),
        accel_group, GDK_l, GDK_CONTROL_MASK );
#ifdef ENABLE_TOPT
    add_menu_item ( field_submenu, "Generate TOPT", G_CALLBACK ( menu_generate_topt ),
        accel_group, GDK_t, GDK_CONTROL_MASK );
#endif
    add_menu_item ( field_submenu, "Current Time", G_CALLBACK ( menu_put_current_time ),
        accel_group, GDK_h, GDK_CONTROL_MASK );
    add_menu_item ( field_submenu, "New password", G_CALLBACK ( menu_generate_password ),
        accel_group, GDK_g, GDK_CONTROL_MASK );
    add_menu_item ( field_submenu, "Random Field", G_CALLBACK ( menu_random_path ),
        accel_group, GDK_x, GDK_CONTROL_MASK );

    gtk_menu_item_set_submenu ( GTK_MENU_ITEM ( field_menu ), field_submenu );
    gtk_menu_shell_append ( GTK_MENU_SHELL ( menu_bar ), field_menu );

    /* Help */
    help_menu = gtk_menu_item_new_with_label ( "Help" );
    help_submenu = gtk_menu_new (  );

    add_menu_item ( help_submenu, "About", G_CALLBACK ( menu_about ), accel_group, GDK_F2, 0 );

    gtk_menu_item_set_submenu ( GTK_MENU_ITEM ( help_menu ), help_submenu );
    gtk_menu_shell_append ( GTK_MENU_SHELL ( menu_bar ), help_menu );

    mainbox = HBOX_NEW;
    app_context.tree_view = create_tree_view_and_model (  );
    app_context.table_view = create_table_view_and_model (  );
    app_context.rootbox = VBOX_NEW;
    gtk_box_pack_start ( GTK_BOX ( app_context.rootbox ), menu_bar, FALSE, FALSE, 0 );
    hbox = HBOX_NEW;
    gtk_box_set_homogeneous ( GTK_BOX ( hbox ), FALSE );
    tree_scrolled_window = gtk_scrolled_window_new ( NULL, NULL );
    gtk_widget_set_size_request ( tree_scrolled_window, 300, 2 );
    gtk_container_add ( GTK_CONTAINER ( tree_scrolled_window ), app_context.tree_view );
    gtk_box_pack_start ( GTK_BOX ( hbox ), tree_scrolled_window, FALSE, TRUE, 2 );
    table_scrolled_window = gtk_scrolled_window_new ( NULL, NULL );
    gtk_container_add ( GTK_CONTAINER ( table_scrolled_window ), app_context.table_view );
    gtk_box_pack_start ( GTK_BOX ( hbox ), table_scrolled_window, TRUE, TRUE, 0 );
    gtk_box_pack_start ( GTK_BOX ( app_context.rootbox ), hbox, TRUE, TRUE, 0 );
    gtk_box_pack_start ( GTK_BOX ( mainbox ), app_context.rootbox, TRUE, TRUE, 0 );

    app_context.authbox = VBOX_NEW;
    entrybox = VBOX_NEW;
    authlabel = gtk_label_new ( "Please Authenticate" );
    label_set_fontsize ( authlabel, 16 );

    gtk_box_pack_start ( GTK_BOX ( entrybox ), authlabel, FALSE, FALSE, 6 );
    app_context.passbox = gtk_entry_new (  );
    gtk_entry_set_visibility ( GTK_ENTRY ( app_context.passbox ), FALSE );
    g_signal_connect ( app_context.passbox, "key-release-event", G_CALLBACK ( auth_on_key_press ),
        app_context.passbox );
    gtk_entry_set_width_chars ( GTK_ENTRY ( app_context.passbox ), 32 );
    gtk_box_pack_start ( GTK_BOX ( entrybox ), app_context.passbox, FALSE, FALSE, 6 );
    gtk_container_set_border_width ( GTK_CONTAINER ( entrybox ), 10 );
    gtk_box_pack_start ( GTK_BOX ( app_context.authbox ), entrybox, TRUE, FALSE, 0 );
    gtk_box_pack_start ( GTK_BOX ( mainbox ), app_context.authbox, TRUE, FALSE, 0 );

    g_signal_connect ( app_context.window, "delete-event", G_CALLBACK ( window_on_deleted ), NULL );
    gtk_container_add ( GTK_CONTAINER ( app_context.window ), mainbox );
    gtk_widget_show_all ( app_context.rootbox );
    gtk_widget_show_all ( entrybox );
    gtk_widget_show ( mainbox );
    gtk_widget_show ( app_context.window );

    if ( argc >= 2 )
    {
        if ( !strcmp ( argv[1], "--allow-empty-passwords-anyway" ) )
        {
            allow_empty_password_anyway = TRUE;

        } else if ( !open_file_by_path ( argv[1] ) )
        {
            return 1;
        }
    }

    if ( !app_context.database && new_file (  ) < 0 )
    {
        return 1;
    }

    g_timeout_add_seconds ( 300, app_lock_handler, NULL );

    gtk_main (  );

    return 0;
}
