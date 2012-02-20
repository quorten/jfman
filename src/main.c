/* Journaling file manager, similar to pcman or Nautilus, but simpler,
   and therefore more easily cross-platform to Microsoft Windows.  */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include <string.h>

#include "support.h"

gchar *package_prefix = PACKAGE_PREFIX;
gchar *package_data_dir = PACKAGE_DATA_DIR;
gchar *package_locale_dir = PACKAGE_LOCALE_DIR;

static GtkWidget *window = NULL;

#define FOLDER_NAME "gnome-fs-directory.png"
#define FILE_NAME "gnome-fs-regular.png"

enum
{
  COL_DISPLAY_NAME,
  COL_PATH,
  COL_PIXBUF,
  COL_IS_DIRECTORY,
  NUM_COLS
};


static GdkPixbuf *file_pixbuf, *folder_pixbuf;
gchar *parent;
GtkToolItem *back_button;
GtkToolItem *forward_button;
GtkToolItem *up_button;
GtkToolItem *undo_button;
GtkToolItem *redo_button;
GtkToolItem *cut_button;
GtkToolItem *copy_button;
GtkToolItem *paste_button;
GtkToolItem *delete_button;

/* Loads the images for the demo and returns whether the operation succeeded */
static gboolean
load_pixbufs (GError **error)
{
  char *filename;

  if (file_pixbuf)
    return TRUE; /* already loaded earlier */

  /*filename = find_pixmap_file (FILE_NAME);
  if (!filename)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
		   "Cannot find demo data file \"%s\"", FILE_NAME);
      return FALSE;
    }

  file_pixbuf = gdk_pixbuf_new_from_file (filename, error);*/
  file_pixbuf = gtk_widget_render_icon (window, GTK_STOCK_FILE,
					GTK_ICON_SIZE_MENU, NULL);
  g_free (filename);
  
  if (!file_pixbuf)
    return FALSE; /* Note that "error" was filled with a GError */
  
  /*filename = find_pixmap_file (FOLDER_NAME);
  if (!filename)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
		   "Cannot find demo data file \"%s\"", FOLDER_NAME);
      return FALSE;
    }

  folder_pixbuf = gdk_pixbuf_new_from_file (filename, error);*/
  folder_pixbuf = gtk_widget_render_icon (window, GTK_STOCK_DIRECTORY,
					  GTK_ICON_SIZE_MENU, NULL);
  g_free (filename);

  return TRUE;
}

static void
fill_store (GtkListStore *store)
{
  GDir *dir;
  const gchar *name;
  gchar *display_name;
  GtkTreeIter iter;
  
  /* First clear the store */
  gtk_list_store_clear (store);

  /* Set the window title to the new directory name.  */
  display_name = g_filename_display_basename (parent);
  gtk_window_set_title (GTK_WINDOW (window), display_name);
  g_free (display_name);

  /* Set the location bar to the current path name.  */
  {
    GList *window_children;
    GtkVBox *vbox;
    GtkBoxChild *box_child;
    GtkWidget *location_bar;
    window_children = gtk_container_get_children (GTK_CONTAINER (window));
    vbox = GTK_VBOX (g_list_nth_data (window_children, 0));
    g_list_free (window_children);
    /* We are assuming that the index of location bar is 1. */
    box_child = (GtkBoxChild *) g_list_nth_data (GTK_BOX (vbox)->children, 1);
    location_bar = box_child->widget;
    gtk_entry_set_text (GTK_ENTRY (location_bar), parent);
  }

  /* Now go through the directory and extract all the file
   * information */
  dir = g_dir_open (parent, 0, NULL);
  if (!dir)
    return;

  name = g_dir_read_name (dir);
  while (name != NULL)
    {
      gchar *path, *display_name;
      gboolean is_dir;
      
      /* We don't ignore hidden files that start with a '.' */
      //if (name[0] != '.')
	{
	  path = g_build_filename (parent, name, NULL);

	  is_dir = g_file_test (path, G_FILE_TEST_IS_DIR);
	  
	  display_name = g_filename_to_utf8 (name, -1, NULL, NULL, NULL);

	  gtk_list_store_append (store, &iter);
	  gtk_list_store_set (store, &iter,
			      COL_PATH, path,
			      COL_DISPLAY_NAME, display_name,
			      COL_IS_DIRECTORY, is_dir,
			      COL_PIXBUF, is_dir ? folder_pixbuf : file_pixbuf,
			      -1);
	  g_free (path);
	  g_free (display_name);
	}

      name = g_dir_read_name (dir);      
    }
}

static gint
sort_func (GtkTreeModel *model,
	   GtkTreeIter  *a,
	   GtkTreeIter  *b,
	   gpointer      user_data)
{
  gboolean is_dir_a, is_dir_b;
  gchar *name_a, *name_b;
  int ret;

  /* We need this function because we want to sort
   * folders before files.
   */

  
  gtk_tree_model_get (model, a,
		      COL_IS_DIRECTORY, &is_dir_a,
		      COL_DISPLAY_NAME, &name_a,
		      -1);

  gtk_tree_model_get (model, b,
		      COL_IS_DIRECTORY, &is_dir_b,
		      COL_DISPLAY_NAME, &name_b,
		      -1);

  if (!is_dir_a && is_dir_b)
    ret = 1;
  else if (is_dir_a && !is_dir_b)
    ret = -1;
  else
    {
      ret = g_utf8_collate (name_a, name_b);
    }

  g_free (name_a);
  g_free (name_b);

  return ret;
}

static GtkListStore *
create_store (void)
{
  GtkListStore *store;

  store = gtk_list_store_new (NUM_COLS,
			      G_TYPE_STRING, 
			      G_TYPE_STRING, 
			      GDK_TYPE_PIXBUF,
			      G_TYPE_BOOLEAN);

  /* Set sort column and function */ 
  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (store),
					   sort_func,
					   NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
					GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
					GTK_SORT_ASCENDING);

  return store;
}

static void
item_activated (GtkTreeView *icon_view,
		GtkTreePath *tree_path,
		GtkTreeViewColumn *column,
		gpointer     user_data)
{
  GtkListStore *store;
  gchar *path;
  GtkTreeIter iter;
  gboolean is_dir;
  
  store = GTK_LIST_STORE (user_data);

  gtk_tree_model_get_iter (GTK_TREE_MODEL (store),
			   &iter, tree_path);
  gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
		      COL_PATH, &path,
		      COL_IS_DIRECTORY, &is_dir,
		      -1);

  if (!is_dir)
    {
      g_free (path);
      return;
    }
  
  /* Replace parent with path and re-fill the model.  */
  g_free (parent);
  parent = path;

  fill_store (store);

  /* Sensitize the up button */
  gtk_widget_set_sensitive (GTK_WIDGET (up_button), TRUE);
}

static void
up_clicked (GtkToolItem *item,
	    gpointer     user_data)
{
  GtkListStore *store;
  gchar *dir_name;

  store = GTK_LIST_STORE (user_data);

  dir_name = g_path_get_dirname (parent);
  g_free (parent);
  
  parent = dir_name;

  fill_store (store);

  /* Maybe de-sensitize the up button */
  gtk_widget_set_sensitive (GTK_WIDGET (up_button),
			    strcmp (parent, "/") != 0);
}

static void
home_clicked (GtkToolItem *item,
	      gpointer     user_data)
{
  GtkListStore *store;

  store = GTK_LIST_STORE (user_data);

  g_free (parent);
  parent = g_strdup (g_get_home_dir ());

  fill_store (store);

  /* Sensitize the up button */
  gtk_widget_set_sensitive (GTK_WIDGET (up_button),
			    TRUE);
}

static void
cut_clicked (GtkToolItem *item,
	     gpointer     user_data)
{
}

static void
copy_clicked (GtkToolItem *item,
	      gpointer     user_data)
{
}

static void
paste_clicked (GtkToolItem *item,
	       gpointer     user_data)
{
}

static void
delete_clicked (GtkToolItem *item,
		gpointer     user_data)
{
}

static void location_bar_activate (GtkEntry *entry,
				   gpointer user_data)
{
  GtkListStore *store;
  const gchar *entry_name;
  gchar *dir_name;

  store = GTK_LIST_STORE (user_data);

  /* Make a copy of the string in the text entry.  */
  entry_name = gtk_entry_get_text (entry);
  dir_name = (gchar *) g_malloc (strlen (entry_name) + 1);
  strcpy (dir_name, entry_name);

  g_free (parent);  
  parent = dir_name;

  fill_store (store);

  /* Maybe de-sensitize the up button */
  gtk_widget_set_sensitive (GTK_WIDGET (up_button),
			    strcmp (parent, "/") != 0);
}

static void close_window(void)
{
  gtk_widget_destroy (window);
  window = NULL;

  g_object_unref (file_pixbuf);
  file_pixbuf = NULL;

  g_object_unref (folder_pixbuf);
  folder_pixbuf = NULL;
}

GtkWidget *
do_iconview (void)
{
  if (!window)
    {
      GError *error;
            
      window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
      gtk_window_set_default_size (GTK_WINDOW (window), 650, 400);
      
      /* gtk_window_set_screen (GTK_WINDOW (window),
			     gtk_widget_get_screen (do_widget)); */
      gtk_window_set_title (GTK_WINDOW (window), "jfman");

      g_signal_connect (window, "destroy",
			G_CALLBACK (close_window), NULL);

      error = NULL;
      if (!load_pixbufs (&error))
	{
	  GtkWidget *dialog;

	  dialog = gtk_message_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_CLOSE,
					   "Failed to load an image: %s",
					   error->message);

	  g_error_free (error);

	  g_signal_connect (dialog, "response",
			    G_CALLBACK (gtk_widget_destroy), NULL);

	  gtk_widget_show (dialog);
	}
      else
	{
	  GtkWidget *sw;
	  GtkWidget *icon_view;
	  GtkListStore *store;
	  GtkWidget *vbox;
	  GtkWidget *tool_bar;
	  GtkWidget *location_bar;
	  GtkToolItem *home_button;
	  GtkCellRenderer *cell;
	  GtkCellRenderer *pbcell;
	  GtkTreeViewColumn *column;
	  GtkTreeSelection *selection;
	  GtkTreeIter iter;
	  
	  vbox = gtk_vbox_new (FALSE, 0);
	  gtk_container_add (GTK_CONTAINER (window), vbox);

	  tool_bar = gtk_toolbar_new ();
	  gtk_box_pack_start (GTK_BOX (vbox), tool_bar, FALSE, FALSE, 0);
	  
	  back_button = gtk_tool_button_new_from_stock (GTK_STOCK_GO_BACK);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), back_button, -1);
	  forward_button = gtk_tool_button_new_from_stock (GTK_STOCK_GO_FORWARD);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), forward_button, -1);
	  up_button = gtk_tool_button_new_from_stock (GTK_STOCK_GO_UP);
	  gtk_tool_item_set_is_important (up_button, TRUE);
	  gtk_widget_set_sensitive (GTK_WIDGET (up_button), FALSE);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), up_button, -1);

	  home_button = gtk_tool_button_new_from_stock (GTK_STOCK_HOME);
	  gtk_tool_item_set_is_important (home_button, TRUE);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), home_button, -1);

	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar),
			      gtk_separator_tool_item_new (), -1);

	  undo_button = gtk_tool_button_new_from_stock (GTK_STOCK_UNDO);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), undo_button, -1);
	  redo_button = gtk_tool_button_new_from_stock (GTK_STOCK_REDO);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), redo_button, -1);
	  cut_button = gtk_tool_button_new_from_stock (GTK_STOCK_CUT);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), cut_button, -1);
	  copy_button = gtk_tool_button_new_from_stock (GTK_STOCK_COPY);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), copy_button, -1);
	  paste_button = gtk_tool_button_new_from_stock (GTK_STOCK_PASTE);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), paste_button, -1);
	  delete_button = gtk_tool_button_new_from_stock (GTK_STOCK_DELETE);
	  gtk_toolbar_insert (GTK_TOOLBAR (tool_bar), delete_button, -1);

	  location_bar = gtk_entry_new ();
	  gtk_box_pack_start (GTK_BOX (vbox), location_bar, FALSE, FALSE, 0);
	  
	  
	  sw = gtk_scrolled_window_new (NULL, NULL);
	  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
					       GTK_SHADOW_ETCHED_IN);
	  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					  GTK_POLICY_AUTOMATIC,
					  GTK_POLICY_AUTOMATIC);
	  
	  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
	  
	  /* Create the store and fill it with the contents of '/' */
	  parent = g_strdup ("/");
	  store = create_store ();
	  fill_store (store);

	  icon_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (icon_view));
	  gtk_tree_selection_set_mode (GTK_TREE_SELECTION (selection),
					    GTK_SELECTION_MULTIPLE);
	  g_object_unref (store);
	  
	  /* Connect the signals of the tool-bar buttons.  */
	  g_signal_connect (up_button, "clicked",
			    G_CALLBACK (up_clicked), store);
	  g_signal_connect (home_button, "clicked",
			    G_CALLBACK (home_clicked), store);
	  g_signal_connect (cut_button, "clicked",
			    G_CALLBACK (cut_clicked), store);
	  g_signal_connect (copy_button, "clicked",
			    G_CALLBACK (copy_clicked), store);
	  g_signal_connect (paste_button, "clicked",
			    G_CALLBACK (paste_clicked), store);
	  g_signal_connect (delete_button, "clicked",
			    G_CALLBACK (delete_clicked), store);

	  /* Connect the signal to the location bar.  */
	  g_signal_connect (location_bar, "activate",
			    G_CALLBACK (location_bar_activate), store);
	  
	  /* We now set which model columns that correspond to the text
	   * and pixbuf of each item
	   */
	  //gtk_icon_view_set_text_column (GTK_ICON_VIEW (icon_view), COL_DISPLAY_NAME);
	  //gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (icon_view), COL_PIXBUF);

	  /* Define the cell rendering for this list.  */
	  column = gtk_tree_view_column_new ();
	  pbcell = gtk_cell_renderer_pixbuf_new ();
	  gtk_tree_view_column_pack_start (column, pbcell, FALSE);
	  gtk_tree_view_column_set_attributes (column, pbcell,
					       "pixbuf", COL_PIXBUF, 
					       NULL);
	  cell = gtk_cell_renderer_text_new ();
	  gtk_tree_view_column_pack_start (column, cell, TRUE);
	  gtk_tree_view_column_set_attributes (column, cell,
					       "text", COL_DISPLAY_NAME,
					       NULL);
  
	  gtk_tree_view_append_column (GTK_TREE_VIEW (icon_view),
				       GTK_TREE_VIEW_COLUMN (column));

	  gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter);
	  gtk_tree_selection_select_iter (GTK_TREE_SELECTION (selection), &iter);
	  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (icon_view), TRUE);
	  gtk_tree_view_column_set_title (column, "Name");

	  g_signal_connect (icon_view, "row_activated",
			    G_CALLBACK (item_activated), store);
	  gtk_container_add (GTK_CONTAINER (sw), icon_view);

	  gtk_widget_grab_focus (icon_view);
	}
    }
  
  if (!GTK_WIDGET_VISIBLE (window))
    gtk_widget_show_all (window);
  else
    {
      gtk_widget_destroy (window);
      window = NULL;
    }

  return window;
}

int
main (int argc, char *argv[])
{
  gchar *pixmap_dir;
#ifdef G_OS_WIN32
  package_prefix = g_win32_get_package_installation_directory (NULL, NULL);
  package_data_dir = g_build_filename (package_prefix, "share", NULL);
  package_locale_dir =
    g_build_filename (package_prefix, "share", "locale", NULL);
#endif

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, package_locale_dir);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  gtk_set_locale ();
  gtk_init (&argc, &argv);

  pixmap_dir = g_build_filename (package_data_dir, PACKAGE, "pixmaps", NULL);
  add_pixmap_directory (pixmap_dir);
  g_free (pixmap_dir);

  do_iconview ();
  g_signal_connect ((gpointer) window, "destroy",
		    G_CALLBACK (gtk_main_quit), NULL);

  gtk_main ();

#ifdef G_OS_WIN32
  g_free (package_prefix);
  g_free (package_data_dir);
  g_free (package_locale_dir);
#endif

  return 0;
}

#ifdef _MSC_VER
#include <windows.h>

int WINAPI
WinMain (HINSTANCE hInstance,
	 HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  return main (__argc, __argv);
}
#endif
