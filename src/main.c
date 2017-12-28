/* Journaling file manager, similar to pcman or Nautilus, but simpler,
   and therefore more easily cross-platform to Microsoft Windows.  */

/* TODO FIXME: Fix keybinding override bug for location bar.  */

/* TODO FIXME: Add code to support backing off on rename and create
   errors.  */

/* TODO FIXME: Fix drag-and-drop so that clicking to start the drag
   does not change the selection.  */

/* TODO FIXME: Drag-and-drop completion is buggy such that after one
   such operation is complete, some time later, a drag-back animation
   will be shown, even though the operation completed
   successfully.  */

/* TODO FIXME: Refactor this code to remove duplicate code now that
   the application is working.  */

/* TODO FIXME: Use a separate action group for the global actions.  */

/* TODO FIXME: CHECK areas of `strcmp ()' where we may need to handle
   UTF-8 but we are not doing so.  */

/* TODO FIXME: Renaming a folder and clicking away in the icon view is
   registered as a "cancel" rather than a "Finish rename."  */

/* TODO FIXME: 10-minute autosave, though easy to implement, is not
   sufficient for this application.  Writes to the log file should be
   immediate, so that crashes result in no loss of data.  This,
   however, means that we need to keep track of write buffers
   internally so that we can truncate exactly the number of characters
   of a command undo.  */

/* TODO FIXME: Add support for computing total disk space consumed by
   a directory.  Optionally, compute best and worst-case consumption
   of that directory depending on linked contents within it.  */

/* TODO FIXME: Make sure you have a practical method for loading
   files.  Should a load clear the undo history?  It only makes sense
   to add to the undo history if any pending redoes are cleared.  */

/* TODO FIXME: Add a "New" method for clearing the undo history.  */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* If this is what it takes to get `struct stat64'...  */
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <gtk/gtk.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "support.h"

/* For FILE in `cmdline.h' */
#include <stdio.h>
#include "bool.h"
#include "cmdline.h"

enum
{
  COL_DISPLAY_NAME,
  COL_FS_NAME,
  COL_PIXBUF,
  COL_IS_DIRECTORY,
  COL_SIZE,
  COL_DISP_SIZE,
  COL_MTIME,
  NUM_COLS
};

typedef enum JFMViewMode_tag JFMViewMode;
enum JFMViewMode_tag
{
  JFM_VIEW_ICONS,
  JFM_VIEW_SM_ICONS,
  JFM_VIEW_DETAILS,
};

typedef enum JFMOper_tag JFMOper;
enum JFMOper_tag
{
  JFM_RENAME,
  JFM_CREATE,
  JFM_DELETE,
  JFM_MOVE,
};

typedef struct BrowseHist_tag BrowseHist;
struct BrowseHist_tag
{
  gchar *parent;
  gdouble scroll_pos;
};

typedef struct UndoFile_tag UndoFile;
struct UndoFile_tag
{
  gchar *name;
  /* When loading command lists, filling in the metadata fields
     (`is_dir', etc.) may be deferred until the action is actually
     performed.  This is helpful when loading a list of already
     executed commands, because otherwise, one would have to perform
     reverse analytics.  */
  gboolean lazy;
  gboolean is_dir;
  gint64 file_size;
  gchar *disp_size;
  gchar *disp_mtime;
  /* In virtual filesystems, we can save virtual filesystem nodes to
     make deletes reversible.  */
  void *fsnode;
};

typedef struct UndoHist_tag UndoHist;
struct UndoHist_tag
{
  /* TODO FIXME: Storing a copy of parent in each undo entry
     artificially multiplies memory consumption when the path name of
     the parent directory is long.  Prefer reference-counted strings
     here instead.  */
  JFMOper oper;
  gchar *parent;
  union
  {
    struct UndoRename_tag
    {
      gchar *name;
      gchar *new_name;
    } rename;
    struct UndoCreate_tag
    {
      GArray *files;
    } create;
    struct UndoDelete_tag
    {
      GArray *files;
    } delete;
    struct UndoMove_tag
    {
      GArray *files;
      gchar *new_parent;
    } move;
  } t;
};

typedef struct UndoContext_tag UndoContext;
struct UndoContext_tag
{
  GArray *stack;
  guint pos;
  /* We need to store pointers to all the context structures since
     undo is global and can affect all visible windows.  */
  GArray *contexes;
};

typedef struct JFMContext_tag JFMContext;
typedef JFMContext* JFMContext_ptr;
struct JFMContext_tag
{
  gchar *parent; /* current working directory */
  JFMViewMode view_mode;
  gboolean view_menubar;
  gboolean view_toolbar;
  gboolean view_location_bar;
  gboolean full_path_title;
  GArray *browshist_stack;
  guint browshist_pos;
  gboolean creating_file;
  gboolean finishing_rename;

  GtkWidget *window;
  GtkWidget *menu_bar;
  GtkWidget *tool_bar;
  GtkListStore *store;
  GtkWidget *location_bar;
  GtkWidget *sw;
  GtkWidget *icon_view;
  GtkTreeViewColumn *file_column;
  GtkCellRenderer *file_cell;
  GtkAction *back_action;
  GtkAction *forward_action;
  GtkAction *up_action;
  GtkAction *cut_action;
  /* GtkAction *copy_action; */
  GtkAction *rename_action;
  GtkAction *delete_action;

  /* This is used for our scroll bar position hack.  */
  GtkTreePath *temp_path;
};

gchar *package_prefix = PACKAGE_PREFIX;
gchar *package_data_dir = PACKAGE_DATA_DIR;
gchar *package_locale_dir = PACKAGE_LOCALE_DIR;

#define FOLDER_NAME "gnome-fs-directory.png"
#define FILE_NAME "gnome-fs-regular.png"

static GdkPixbuf *file_pixbuf, *folder_pixbuf;
static GdkPixbuf *file_sm_pixbuf, *folder_sm_pixbuf;

/* Typically, we will be operating on a virtual filesystem where
   deletes are fully reversible.  Because our application requires
   that all filesystem actions be reversible for proper operation, we
   have the option to disable deletes if we are operating on a real
   filesystem.  */
static gboolean g_disable_delete = FALSE;

/* Shout dot-files be hidden from directory listings?  */
static gboolean g_hide_dotfiles = FALSE;

static int g_num_windows = 0;

static UndoContext g_undoctx;

static UndoHist g_cut_buffer;

static JFMContext *g_drag_src;

static gchar *g_load_cwd = NULL;

static const gchar *ui_info =
"<ui>"
"  <menubar name='MenuBar'>"
"    <menu action='FileMenu'>"
"      <menuitem action='NewWindow'/>"
"      <menuitem action='Open'/>"
"      <menuitem action='Save'/>"
"      <menuitem action='SaveAs'/>"
"      <separator/>"
"      <menuitem action='CloseWindow'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='Undo'/>"
"      <menuitem action='Redo'/>"
"      <separator/>"
"      <menuitem action='NewFolder'/>"
"      <menuitem action='NewFile'/>"
"      <separator/>"
"      <menuitem action='Cut'/>"
/* "      <menuitem action='Copy'/>" */
"      <menuitem action='Paste'/>"
"      <menuitem action='Rename'/>"
"      <separator/>"
"      <menuitem action='Delete'/>"
"      <separator/>"
"      <menuitem action='SelectAll'/>"
"      <menuitem action='InvertSelection'/>"
"    </menu>"
"    <menu action='GoMenu'>"
"      <menuitem action='Back'/>"
"      <menuitem action='Forward'/>"
"      <menuitem action='Up'/>"
"      <menuitem action='Home'/>"
"      <menuitem action='AddressBar'/>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='Icons'/>"
"      <menuitem action='SmallIcons'/>"
"      <menuitem action='Details'/>"
"      <separator/>"
"      <menuitem action='HideDotFiles'/>"
"      <separator/>"
"      <menuitem action='ViewMenuBar'/>"
"      <menuitem action='ViewToolBar'/>"
"      <menuitem action='ViewLocationBar'/>"
"      <menuitem action='FullPathTitle'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
"  <toolbar name='ToolBar'>"
"    <toolitem action='NewWindow'/>"
"    <toolitem action='Back'/>"
"    <toolitem action='Forward'/>"
"    <toolitem action='Up'/>"
"    <separator/>"
"    <toolitem action='Undo'/>"
"    <toolitem action='Redo'/>"
"    <separator/>"
"    <toolitem action='NewFolder'/>"
"    <toolitem action='Rename'/>"
"    <separator/>"
"    <toolitem action='Delete'/>"
"  </toolbar>"
"</ui>";

/* Global action group.  */
GtkActionGroup *g_action_group = NULL;
GtkAction *g_undo_action;
GtkAction *g_redo_action;
GtkAction *g_paste_action;

/** Keeps track of whether the user has unsaved changes in the current
    file.  */
gboolean file_modified = FALSE;
/** Keeps track of whether all currently unsaved changes have been
    autosaved.  */
gboolean file_autosaved = FALSE;
/** Keeps track of the last visited folder in the GTK+ file chooser
    for convenience for the user.  */
gchar *last_folder = NULL;
/** Keeps track of the currently loaded filename.  */
gchar *loaded_fname = NULL;

gboolean
save_as (JFMContext *context);
void remove_autosave (void);

/**
 * Display the about dialog for the Journaling File Manager.
 */
static void
display_about_box (GtkWidget *window)
{
  const gchar *license =
"Copyright (C) 2012, 2013, 2017 Andrew Makousky\n"
"\n"
"This program is free software; you can redistribute it and/or modify\n"
"it under the terms of the GNU General Public License as published by\n"
"the Free Software Foundation; either version 2 of the License, or\n"
"(at your option) any later version.\n"
"\n"
"This program is distributed in the hope that it will be useful,\n"
"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
"GNU General Public License for more details.\n"
"\n"
"You should have received a copy of the GNU General Public License\n"
"along with this program; if not, write to the Free Software\n"
"Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n";

  GdkPixbuf *pixbuf = NULL /* create_pixbuf (FOLDER_NAME) */;

  gtk_show_about_dialog (GTK_WINDOW (window),
	 "program-name", _("Journaling File Manager"),
	 "version", PACKAGE_VERSION,
	 "copyright", "© 2012, 2013, 2017 Andrew Makousky",
	 "license", license,
	 "comments",
_("\"jfman\" is a Journaling File Manager, a graphical file manager " \
"that logs your file management commands to a text file.  Due to the "\
"primary emphasis on the journaling feature, this file manager is " \
"highly minimalistic in other features."),
	 "logo", pixbuf,
         "title", _("About JFman"),
	 NULL);

  /* g_object_unref (pixbuf); */
}

/**
 * Isolates fprintf functions from save_sh_project().
 */
void
do_save_printing (JFMContext *context, FILE *fp)
{
  GArray *stack = g_undoctx.stack;
  unsigned stack_len = stack->len;
  gchar *old_parent = NULL;
  unsigned i;
  for (i = 0; i < stack_len; i++)
    {
      UndoHist *elem = &g_array_index (stack, UndoHist, i);
      gchar *op_cmd = NULL;
      if (old_parent == NULL || strcmp(old_parent, elem->parent))
	{
	  char *argv[] = { "cd", elem->parent };
	  old_parent = elem->parent;
	  write_cmdline (fp, 2, argv, 0);
	}
      switch (elem->oper)
	{
	case JFM_RENAME:
	  {
	    char *argv[] = { "mv", elem->t.rename.name,
			     elem->t.rename.new_name };
	    write_cmdline (fp, 3, argv, 0);
	    break;
	  }
	case JFM_MOVE:
	case JFM_CREATE:
	case JFM_DELETE:
	  if (elem->oper == JFM_MOVE)
	      op_cmd = "mvt";
	  if (elem->oper == JFM_CREATE)
	    {
	      if (g_array_index (elem->t.create.files, UndoFile, 0).is_dir)
		op_cmd = "mkdir";
	      else
		op_cmd = "touch";
	    }
	  if (elem->oper == JFM_DELETE)
	    {
	      /* TODO FIXME: This needs to treat each file
		 individually, as they may be different.  Alas, that
		 makes for a more complicated loop structure.  */
	      if (g_array_index (elem->t.delete.files, UndoFile, 0).is_dir)
		op_cmd = "rmdir";
	      else
		op_cmd = "rm";
	    }
	  {
	    GArray *argv = g_array_new (FALSE, FALSE, sizeof (gchar*));
	    GArray *move_files = elem->t.move.files;
	    unsigned move_files_len = move_files->len;
	    unsigned j;
	    g_array_append_val (argv, op_cmd);
	    for (j = 0; j < move_files_len; j++)
	      g_array_append_val
		(argv, g_array_index (move_files, UndoFile, j).name);
	    if (elem->oper == JFM_MOVE)
	      g_array_append_val (argv, elem->t.move.new_parent);
	    write_cmdline (fp, argv->len, (char **) argv->data, 0);
	    g_array_free (argv, TRUE);
	    break;
	  }
	}
    }
}

/**
 * Saves a file management command list.
 *
 * @param filename the file name to save to
 * @return TRUE if save was successful, FALSE otherwise
 */
gboolean
save_sh_project (JFMContext *context, char *filename)
{
  FILE *fp;

  fp = fopen (filename, "w");
  if (fp == NULL)
    goto error;

  do_save_printing (context, fp);

  if (fclose (fp) == EOF)
    goto error;
  return TRUE;

 error:
  {
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new_with_markup
      (GTK_WINDOW (context->window),
       GTK_DIALOG_DESTROY_WITH_PARENT,
       GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
       _("<b><big>An error occurred while saving your " \
	 "file.</big></b>\n\n%s"),
       strerror(errno));
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    return FALSE;
  }
}

gboolean
do_autosave (gpointer user_data)
{
  if (loaded_fname != NULL && file_modified && !file_autosaved)
    {
      JFMContext *context =
	g_array_index (g_undoctx.contexes, JFMContext_ptr, 0);
      GString *gstr_autosave = g_string_new (NULL);
      gchar *autosave_name;
      g_string_printf (gstr_autosave, "%s%s", loaded_fname, _(".autosave"));
      autosave_name = g_string_free (gstr_autosave, FALSE);
      save_sh_project (context, autosave_name);
      g_free (autosave_name);
    }
  return TRUE;
}

void
remove_autosave (void)
{
  if (loaded_fname != NULL)
    {
      GString *gstr_autosave = g_string_new (NULL);
      gchar *autosave_name;
      g_string_printf (gstr_autosave, "%s%s", loaded_fname, _(".autosave"));
      autosave_name = g_string_free (gstr_autosave, FALSE);
      unlink (autosave_name);
      g_free (autosave_name);
    }
}

int proc_chdir(int argc, char *argv[])
{
  if (argc != 2)
    return 1;
  g_free (g_load_cwd);
  g_load_cwd = g_strdup (argv[1]);
  return 0;
}

int proc_touch(int argc, char *argv[])
{
  UndoHist action;
  unsigned i;
  if (argc < 2)
    return 1;
  action.oper = JFM_CREATE;
  action.parent = g_strdup (g_load_cwd);
  action.t.create.files = g_array_new (FALSE, FALSE, sizeof (UndoFile));
  for (i = 1; i < argc; i++)
    {
      UndoFile file;
      file.name = g_strdup (argv[i]);
      file.lazy = TRUE;
      file.is_dir = FALSE;
      file.file_size = 0;
      file.disp_size = NULL;
      file.disp_mtime = NULL;
      file.fsnode = NULL;
      g_array_append_val (action.t.create.files, file);
    }
  g_array_append_val (g_undoctx.stack, action);
  g_undoctx.pos++;
  return 0;
}

int proc_mv(int argc, char *argv[])
{
  UndoHist action;
  if (argc != 3)
    return 1;
  action.oper = JFM_RENAME;
  action.parent = g_strdup (g_load_cwd);
  action.t.rename.name = g_strdup (argv[1]);
  action.t.rename.new_name = g_strdup (argv[2]);
  g_array_append_val (g_undoctx.stack, action);
  g_undoctx.pos++;
  return 0;
}

int proc_mvt(int argc, char *argv[])
{
  UndoHist action;
  unsigned i;
  if (argc < 2)
    return 1;
  action.oper = JFM_MOVE;
  action.parent = g_strdup (g_load_cwd);
  action.t.move.files = g_array_new (FALSE, FALSE, sizeof (UndoFile));
  for (i = 1; i < argc - 1; i++)
    {
      UndoFile file;
      file.name = g_strdup (argv[i]);
      file.lazy = TRUE;
      file.is_dir = FALSE;
      file.file_size = 0;
      file.disp_size = NULL;
      file.disp_mtime = NULL;
      file.fsnode = NULL;
      g_array_append_val (action.t.move.files, file);
    }
  action.t.move.new_parent = g_strdup (argv[i]);
  g_array_append_val (g_undoctx.stack, action);
  g_undoctx.pos++;
  return 0;
}

int proc_rm(int argc, char *argv[])
{
  UndoHist action;
  unsigned i;
  if (argc < 2)
    return 1;
  action.oper = JFM_DELETE;
  action.parent = g_strdup (g_load_cwd);
  action.t.delete.files = g_array_new (FALSE, FALSE, sizeof (UndoFile));
  for (i = 1; i < argc; i++)
    {
      UndoFile file;
      file.name = g_strdup (argv[i]);
      file.lazy = TRUE;
      file.is_dir = FALSE;
      file.file_size = 0;
      file.disp_size = NULL;
      file.disp_mtime = NULL;
      file.fsnode = NULL;
      g_array_append_val (action.t.delete.files, file);
    }
  g_array_append_val (g_undoctx.stack, action);
  g_undoctx.pos++;
  return 0;
}

int proc_mkdir(int argc, char *argv[])
{
  UndoHist action;
  unsigned i;
  if (argc < 2)
    return 1;
  action.oper = JFM_CREATE;
  action.parent = g_strdup (g_load_cwd);
  action.t.create.files = g_array_new (FALSE, FALSE, sizeof (UndoFile));
  for (i = 1; i < argc; i++)
    {
      UndoFile file;
      file.name = g_strdup (argv[i]);
      file.lazy = TRUE;
      file.is_dir = TRUE;
      file.file_size = 0;
      file.disp_size = NULL;
      file.disp_mtime = NULL;
      file.fsnode = NULL;
      g_array_append_val (action.t.create.files, file);
    }
  g_array_append_val (g_undoctx.stack, action);
  g_undoctx.pos++;
  return 0;
}

int proc_rmdir(int argc, char *argv[])
{
  UndoHist action;
  unsigned i;
  if (argc < 2)
    return 1;
  action.oper = JFM_DELETE;
  action.parent = g_strdup (g_load_cwd);
  action.t.delete.files = g_array_new (FALSE, FALSE, sizeof (UndoFile));
  for (i = 1; i < argc; i++)
    {
      UndoFile file;
      file.name = g_strdup (argv[i]);
      file.lazy = TRUE;
      file.is_dir = TRUE;
      file.file_size = 0;
      file.disp_size = NULL;
      file.disp_mtime = NULL;
      file.fsnode = NULL;
      g_array_append_val (action.t.delete.files, file);
    }
  g_array_append_val (g_undoctx.stack, action);
  g_undoctx.pos++;
  return 0;
}

/**
 * Loads a file management command list.
 *
 * Opens, reads, and parses a file management command list.  File
 * management command lists are plain text files, typically saved with
 * a .sh extension.
 * @param filename the name of the file to load
 * @return TRUE on successful load, FALSE on error.
 */
gboolean
load_sh_project (JFMContext *context, char *filename)
{
  gboolean retval = FALSE;
  FILE *fp;

  fp = fopen (filename, "r");
  if (fp == NULL)
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new_with_markup
	(NULL,
	 GTK_DIALOG_DESTROY_WITH_PARENT,
	 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
	 _("<b><big>Your file could not be opened.</big></b>\n\n" \
	   "%s"),
	 strerror(errno));
      gtk_window_set_title (GTK_WINDOW (dialog), _("jfman"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      return FALSE;
    }

  {
#define NUM_CMDS 8
    const char *commands[NUM_CMDS] =
      { "cd", "chdir", "touch", "mv", "mvt", "rm", "mkdir", "rmdir" };
    const CmdFunc cmdfuncs[NUM_CMDS] = {
      proc_chdir, proc_chdir, proc_touch, proc_mv, proc_mvt, proc_rm,
      proc_mkdir, proc_rmdir };
    int result = proc_cmd_dispatch (fp, NUM_CMDS, commands, cmdfuncs, true);
    if (result != 0)
      { retval = FALSE; goto cleanup; }
  }

  retval = TRUE;
 cleanup:
  g_free (g_load_cwd); g_load_cwd = NULL;

  /* Now fix the undo/redo action button statuses.  */
  gtk_action_set_sensitive (g_undo_action,
			    (g_undoctx.pos <= 0) ? FALSE : TRUE);
  gtk_action_set_sensitive
    (g_redo_action,
     (g_undoctx.pos >= g_undoctx.stack->len)
     ? FALSE : TRUE);

  if (!retval)
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new_with_markup
	(NULL,
	 GTK_DIALOG_DESTROY_WITH_PARENT,
	 GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
	 _("<b><big>A syntax error was found in your file.</big></b>\n\n" \
	   "A blank template will be loaded instead."));
      gtk_window_set_title (GTK_WINDOW (dialog), _("jfman"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      return FALSE;
    }
  fclose (fp);
  return retval;
}

/**
 * Creates a file management command list.
 */
void
new_sh_project (void)
{
}

/**
 * Ask the user if they want to save their file before continuing.
 *
 * @return TRUE to continue, FALSE to cancel
 */
gboolean check_save (JFMContext *context, gboolean closing)
{
  GtkWidget *dialog;
  const gchar *prompt_msg;
  const gchar *continue_msg;
  GtkResponseType result;

  if (!file_modified)
    return TRUE;

  if (closing)
    {
      prompt_msg = _("<b><big>Save changes to the current file before " \
       "closing?</big></b>\n\nIf you close without saving, " \
       "your changes will be discarded.");
      continue_msg = _("Close _without saving");
    }
  else
    {
      prompt_msg = _("<b><big>Save changes to the current file before " \
       "continuing?</big></b>\n\nIf you continue without saving, " \
       "your changes will be discarded.");
      continue_msg = _("Continue _without saving");
    }

  dialog = gtk_message_dialog_new (GTK_WINDOW (context->window),
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
				   NULL);
  gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), prompt_msg);
  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
		  continue_msg, GTK_RESPONSE_CLOSE,
		  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		  GTK_STOCK_SAVE, GTK_RESPONSE_YES,
		  NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);
  result = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  if (result == GTK_RESPONSE_CLOSE)
    return TRUE;
  else if (result == GTK_RESPONSE_CANCEL)
    return FALSE;
  else if (result == GTK_RESPONSE_YES)
    return save_as (context);
  /* Unknown response?  Do nothing.  */
  return FALSE;
}

/**
 * Save a file with a specific name from the GUI.
 */
gboolean
save_as (JFMContext *context)
{
  GtkFileFilter *filter = gtk_file_filter_new ();
  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save File"),
				 GTK_WINDOW (context->window),
				 GTK_FILE_CHOOSER_ACTION_SAVE,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_SAVE_AS, GTK_RESPONSE_ACCEPT,
				 NULL);
  gtk_file_filter_set_name
    (filter, _("File management command lists (*.sh)"));
  gtk_file_filter_add_pattern (filter, "*.sh");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
  if (last_folder != NULL)
    {
      gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog),
					   last_folder);
    }

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
      gboolean result;
      gchar *filename =
	gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      unsigned filename_len = strlen (filename);
      g_free (last_folder);
      last_folder = gtk_file_chooser_get_current_folder
	                   (GTK_FILE_CHOOSER (dialog));
      gtk_widget_destroy (dialog);
      if (filename_len <= 3 ||
	  strcmp(&filename[filename_len-3], ".sh"))
	{
	  filename = g_realloc (filename,
				sizeof(gchar) * (filename_len + 6));
	  strcpy(&filename[filename_len], ".sh");
	}
      result = save_sh_project (context, filename);
      if (!result)
	{
	  g_free (filename);
	  return FALSE;
	}
      file_modified = FALSE;
      remove_autosave ();
      g_free(loaded_fname); loaded_fname = NULL;
      loaded_fname = filename;
      return TRUE;
    }
  else
    gtk_widget_destroy (dialog);
  return FALSE;
}

/**
 * Open a file from the GUI.
 */
void
open_file (JFMContext *context)
{
  GtkFileFilter *filter = gtk_file_filter_new ();
  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Open File"),
				 GTK_WINDOW (context->window),
				 GTK_FILE_CHOOSER_ACTION_OPEN,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				 NULL);
  gtk_file_filter_set_name
    (filter, _("File management command lists (*.sh)"));
  gtk_file_filter_add_pattern (filter, "*.sh");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
  if (last_folder != NULL)
    {
      gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog),
					   last_folder);
    }

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
      gchar *filename =
	gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      g_free (last_folder);
      last_folder = gtk_file_chooser_get_current_folder
	                   (GTK_FILE_CHOOSER (dialog));
      gtk_widget_destroy (dialog);
      g_free(loaded_fname); loaded_fname = NULL;
      /* TODO FIXME: Clear GUI context as appropriate here.  */
      /* unselect_fund_freq (g_fund_set);
      free_wv_editors ();
      init_wv_editors (); */
      if (!load_sh_project (context, filename))
	{
	  /* TODO FIXME: */
	  /* free_wv_editors (); */
	  g_free (filename);
	  /* init_wv_editors (); */
	  new_sh_project ();
	}
      else
	loaded_fname = filename;
      /* select_fund_freq (g_fund_set); */
    }
  else
    gtk_widget_destroy (dialog);
}

/* Loads the images for the demo and returns whether the operation succeeded */
static gboolean
load_pixbufs (GtkWidget *window, GError **error)
{
  GtkIconSize icon_size = GTK_ICON_SIZE_DIALOG;
  char *filename;

  if (file_pixbuf)
    return TRUE; /* already loaded earlier */

  /*filename = find_pixmap_file (FILE_NAME);
  if (!filename)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
		   _("Cannot find demo data file \"%s\""), FILE_NAME);
      return FALSE;
    }

  file_pixbuf = gdk_pixbuf_new_from_file (filename, error);*/
  file_pixbuf = gtk_widget_render_icon (window, GTK_STOCK_FILE,
					GTK_ICON_SIZE_DIALOG, NULL);
  file_sm_pixbuf = gtk_widget_render_icon (window, GTK_STOCK_FILE,
					   GTK_ICON_SIZE_MENU, NULL);
  g_free (filename);

  if (!file_pixbuf)
    return FALSE; /* Note that "error" was filled with a GError */

  /*filename = find_pixmap_file (FOLDER_NAME);
  if (!filename)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
		   _("Cannot find demo data file \"%s\""), FOLDER_NAME);
      return FALSE;
    }

  folder_pixbuf = gdk_pixbuf_new_from_file (filename, error);*/
  folder_pixbuf = gtk_widget_render_icon (window, GTK_STOCK_DIRECTORY,
					  GTK_ICON_SIZE_DIALOG, NULL);
  folder_sm_pixbuf = gtk_widget_render_icon (window, GTK_STOCK_DIRECTORY,
					     GTK_ICON_SIZE_MENU, NULL);
  g_free (filename);

  return TRUE;
}

/* Fill in deferred lazy stats for an UndoFile.  */
static void
fill_lazy_stats (gchar *parent, UndoFile *file)
{
  gchar *path = g_build_filename (parent, file->name, NULL);
  gint64 file_size;
  gchar *disp_size, *disp_mtime;
  struct stat64 sbuf;
#define NUM_SUFFIX 7
  gdouble calc_size;
  char scale_suffixes[NUM_SUFFIX] =
    { 'K', 'M', 'G', 'T', 'P', 'E', 'Z' };
  unsigned suffix;
  GString *gstr_disp_size;
  struct tm *date;

  int result = lstat64 (path, &sbuf);
  g_free (path);
  if (result == -1)
    return;

  /* Find the scale suffix that is one beyond the
     appropriate one to display the file size.  */
  file_size = sbuf.st_size;
  calc_size = sbuf.st_size;
  for (suffix = 0; suffix < NUM_SUFFIX &&
	 calc_size >= 1024; suffix++)
    calc_size /= 1024;
  gstr_disp_size = g_string_new (NULL);
  if (suffix > 0)
    g_string_printf (gstr_disp_size, "%.3g%c",
		     calc_size, scale_suffixes[suffix-1]);
  else
    g_string_printf (gstr_disp_size, "%g", calc_size);
  disp_size = g_string_free (gstr_disp_size, FALSE);

  /* Format the modified date for display.  */
  date = localtime (&(sbuf.st_mtime));
  date->tm_sec = (date->tm_sec >= 60) ? 59 : date->tm_sec;
  /* = "-yyyyyyyyyy-mm-dd hh:mm.ss"; */
  disp_mtime = (gchar *) g_malloc (sizeof (gchar) * 27);
  g_sprintf (disp_mtime, "%d-%02d-%02d %02d:%02d:%02d",
	     1900 + date->tm_year, date->tm_mon + 1, date->tm_mday,
	     date->tm_hour, date->tm_min, date->tm_sec);

  file->lazy = FALSE;
  file->is_dir = S_ISDIR (sbuf.st_mode) ? TRUE : FALSE;
  file->file_size = file_size;
  file->disp_size = disp_size;
  file->disp_mtime = disp_mtime;
}

static void
fill_store (JFMContext *context)
{
  gint sort_column_id;
  GtkSortType sort_order;
  GDir *dir;
  const gchar *name;
  gchar *display_name;
  GtkTreeIter iter;

  /* Disable sorting so that we don't get a performance hit.  */
  gtk_tree_sortable_get_sort_column_id
    (GTK_TREE_SORTABLE (context->store),
     &sort_column_id,
     &sort_order);
  gtk_tree_sortable_set_sort_column_id
    (GTK_TREE_SORTABLE (context->store),
     GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
     GTK_SORT_ASCENDING);

  /* First clear the store */
  gtk_list_store_clear (context->store);

  /* Set the window title to the new directory name.  */
  display_name = g_filename_display_basename (context->parent);
  if (context->full_path_title)
    gtk_window_set_title (GTK_WINDOW (context->window), context->parent);
  else
    gtk_window_set_title (GTK_WINDOW (context->window), display_name);
  g_free (display_name);

  /* Set the location bar to the current path name.  */
  gtk_entry_set_text (GTK_ENTRY (context->location_bar), context->parent);

  /* Now go through the directory and extract all the file
   * information */
  dir = g_dir_open (context->parent, 0, NULL);
  if (!dir)
    goto cleanup;

  name = g_dir_read_name (dir);
  while (name != NULL)
    {
      gchar *path, *display_name;
      gboolean is_dir;

      if (!g_hide_dotfiles || name[0] != '.')
	{
	  GdkPixbuf *icon_pixbuf;
	  gint64 file_size;
	  gchar *disp_size, *disp_mtime;
	  path = g_build_filename (context->parent, name, NULL);

	  is_dir = g_file_test (path, G_FILE_TEST_IS_DIR);
	  if (is_dir)
	    {
	      if (context->view_mode == JFM_VIEW_ICONS)
		icon_pixbuf = folder_pixbuf;
	      else
		icon_pixbuf = folder_sm_pixbuf;
	    }
	  else
	    {
	      if (context->view_mode == JFM_VIEW_ICONS)
		icon_pixbuf = file_pixbuf;
	      else
		icon_pixbuf = file_sm_pixbuf;
	    }

	  display_name = g_filename_to_utf8 (name, -1, NULL, NULL, NULL);

	  {
	    struct stat64 sbuf;
#define NUM_SUFFIX 7
	    gdouble calc_size;
	    char scale_suffixes[NUM_SUFFIX] =
	      { 'K', 'M', 'G', 'T', 'P', 'E', 'Z' };
	    unsigned suffix;
	    GString *gstr_disp_size;
	    struct tm *date;

	    lstat64 (path, &sbuf);

	    /* Find the scale suffix that is one beyond the
	       appropriate one to display the file size.  */
	    file_size = sbuf.st_size;
	    calc_size = sbuf.st_size;
	    for (suffix = 0; suffix < NUM_SUFFIX &&
		   calc_size >= 1024; suffix++)
	      calc_size /= 1024;
	    gstr_disp_size = g_string_new (NULL);
	    if (suffix > 0)
	      g_string_printf (gstr_disp_size, "%.3g%c",
			       calc_size, scale_suffixes[suffix-1]);
	    else
	      g_string_printf (gstr_disp_size, "%g", calc_size);
	    disp_size = g_string_free (gstr_disp_size, FALSE);

	    /* Format the modified date for display.  */
	    date = localtime (&(sbuf.st_mtime));
	    date->tm_sec = (date->tm_sec >= 60) ? 59 : date->tm_sec;
	    /* = "-yyyyyyyyyy-mm-dd hh:mm.ss"; */
	    disp_mtime = (gchar *) g_malloc (sizeof (gchar) * 27);
	    g_sprintf (disp_mtime, "%d-%02d-%02d %02d:%02d:%02d",
		       1900 + date->tm_year, date->tm_mon + 1, date->tm_mday,
		       date->tm_hour, date->tm_min, date->tm_sec);
	  }

	  gtk_list_store_insert_with_values
	    (context->store, &iter, 0,
	     COL_FS_NAME, name,
	     COL_DISPLAY_NAME, display_name,
	     COL_PIXBUF, icon_pixbuf,
	     COL_IS_DIRECTORY, is_dir,
	     COL_SIZE, file_size,
	     COL_DISP_SIZE, disp_size,
	     COL_MTIME, disp_mtime,
	     -1);
	  g_free (path);
	  g_free (display_name);
	  g_free (disp_size);
	  g_free (disp_mtime);
	}

      name = g_dir_read_name (dir);
    }

 cleanup:
  /* Enable sorting.  */
  gtk_tree_sortable_set_sort_column_id
    (GTK_TREE_SORTABLE (context->store),
     sort_column_id,
     sort_order);
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
			      G_TYPE_BOOLEAN,
			      G_TYPE_INT64,
			      G_TYPE_STRING,
			      G_TYPE_STRING);

  /* Set sort column and function */
  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (store),
					   sort_func,
					   NULL, NULL);
  gtk_tree_sortable_set_sort_column_id
    (GTK_TREE_SORTABLE (store),
     GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
     GTK_SORT_ASCENDING);

  return store;
}

static void
change_directory (JFMContext *context)
{
  fill_store (context);

  /* Maybe de-sensitize the up button */
  gtk_action_set_sensitive (context->up_action,
			    strcmp (context->parent, "/") != 0);
}

/* `parent' must be dynamically allocated, and memory ownership of
   `parent' passes into this function.  The history stack maintains
   memory ownership.  */
static void
push_directory (JFMContext *context, gchar *new_parent)
{
  GtkAdjustment *vadj;
  /* Clear the forward locations from the stack, if applicable.  */
  if (context->browshist_stack->len > 0 &&
      context->browshist_pos < context->browshist_stack->len - 1)
    {
      unsigned i;
      for (i = context->browshist_pos + 1;
	   i < context->browshist_stack->len; i++)
	{
	  gchar *parent = g_array_index (context->browshist_stack,
					 BrowseHist, i).parent;
	  g_free (parent);
	}
      g_array_remove_range (context->browshist_stack,
			    context->browshist_pos + 1,
			    context->browshist_stack->len -
			      (context->browshist_pos + 1));
      gtk_action_set_sensitive (context->forward_action, FALSE);
    }
  {
    vadj = gtk_scrolled_window_get_vadjustment
      (GTK_SCROLLED_WINDOW (context->sw));
    BrowseHist elem;

    if (context->browshist_stack->len > 0)
      /* Save the scroll position of the current history item.  */
      g_array_index (context->browshist_stack, BrowseHist,
		     context->browshist_pos).scroll_pos =
	gtk_adjustment_get_value (vadj);

    /* Add the history stack item for the new directory.  */
    elem.parent = new_parent;
    elem.scroll_pos = 0;
    g_array_append_val (context->browshist_stack, elem);
    if (context->browshist_stack->len > 1)
      {
	context->browshist_pos++;
	gtk_action_set_sensitive (context->back_action, TRUE);
      }
  }

  context->parent = new_parent;

  change_directory (context);
  gtk_adjustment_set_value (vadj, 0);
}

/* This is annoying.  Because of some delayed scroll update behavior
   when we change the list contents, we have to wait until other
   events are triggered before we set the scroll bar to its desired
   position.  */
static gboolean
finish_set_scroll (gpointer user_data)
{
  JFMContext *context = (JFMContext *) user_data;
  GtkAdjustment *vadj;
  gdouble scroll_pos;
  vadj =
    gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (context->sw));
  scroll_pos = g_array_index (context->browshist_stack, BrowseHist,
			      context->browshist_pos).scroll_pos;
  gtk_adjustment_set_value (vadj, scroll_pos);
  return FALSE;
}

/* Again, another hack, this time because the Icon View widget needs
   some signal cycle between creating a new folder and activating
   editing on it.  */
static gboolean
finish_new_folder (gpointer user_data)
{
  JFMContext *context = (JFMContext *) user_data;  
  gtk_icon_view_set_cursor (GTK_ICON_VIEW (context->icon_view),
			    context->temp_path, context->file_cell, TRUE);
  gtk_tree_path_free (context->temp_path);
  context->temp_path = NULL;
  return FALSE;
}

static void
browshist_motion (JFMContext *context, gboolean forward)
{
  GtkAdjustment *vadj;
  BrowseHist *elem;
  if (!forward && context->browshist_pos <= 0)
    return;
  if (forward && context->browshist_pos >= context->browshist_stack->len - 1)
    return;
  vadj =
    gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (context->sw));

  /* Save the scroll position of the current history item.  */
  g_array_index (context->browshist_stack, BrowseHist,
		 context->browshist_pos).scroll_pos =
    gtk_adjustment_get_value (vadj);

  if (!forward)
    context->browshist_pos--;
  else
    context->browshist_pos++;
  elem = &g_array_index (context->browshist_stack, BrowseHist,
			 context->browshist_pos);
  context->parent = elem->parent;
  change_directory (context);
  /* This is annoying.  Because of some delayed scroll update behavior
     when we change the list contents, we have to wait until other
     events are triggered before we set the scroll bar to its desired
     position.  */
  /* gtk_adjustment_set_value (vadj, elem->scroll_pos); */
  g_idle_add (finish_set_scroll, context);
  gtk_action_set_sensitive (context->back_action,
			    (context->browshist_pos <= 0) ? FALSE : TRUE);
  gtk_action_set_sensitive
    (context->forward_action,
     (context->browshist_pos >= context->browshist_stack->len - 1)
     ? FALSE : TRUE);
}

/* If `free_all' is `FALSE', free all undo entries starting at the
   given position and ending at the end of the array.  If `free_all'
   is `TRUE', then `undohist_pos' must correspond to the current undo
   position, and all undo information is freed.  */
static void
free_undo_entries (GArray *undohist_stack, guint undohist_pos,
		   gboolean free_all)
{
  unsigned i;
  if (free_all)
    i = 0;
  else
    i = undohist_pos;

  for (; i < undohist_stack->len; i++)
    {
      UndoHist *elem = &g_array_index (undohist_stack, UndoHist, i);
      g_free (elem->parent);
      switch (elem->oper)
	{
	case JFM_RENAME:
	  g_free (elem->t.rename.name);
	  g_free (elem->t.rename.new_name);
	  break;
	case JFM_MOVE:
	  g_free (elem->t.move.new_parent);
	case JFM_CREATE:
	case JFM_DELETE:
	  {
	    unsigned j;
	    for (j = 0; j < elem->t.create.files->len; j++)
	      {
		UndoFile *file = &g_array_index
		  (elem->t.create.files, UndoFile, j);
		g_free (file->name);
		g_free (file->disp_size);
		g_free (file->disp_mtime);
		/* We eonly free the FSNode under the condition that
		   it is not referenced from the working
		   filesystem.

		   * Future creates are not in the working filesystem,
		     thus they are deleted.
		   * Future deletes are in the working filesystem,
		     hence they are not deleted.
		   * Past creates are in the working filesystem, hence
		     they are not deleted.
		   * Past deletes are not in the working filesystem,
		     hence they are deleted.
		*/
		if (i >= undohist_pos && elem->oper == JFM_CREATE)
		  g_free (file->fsnode);
		else if (i < undohist_pos && elem->oper == JFM_DELETE)
		  g_free (file->fsnode);
	      }
	    g_array_free (elem->t.create.files, TRUE);
	    break;
	  }
	}
    }
}

typedef struct StoreIndex_tag StoreIndex;
struct StoreIndex_tag
{
  gchar *display_name;
  guint index;
};

static gint
sidx_sort_by_name (gconstpointer a, gconstpointer b)
{
  StoreIndex *sa = (StoreIndex *) a;
  StoreIndex *sb = (StoreIndex *) b;
  return strcmp (sa->display_name, sb->display_name);
}

static gint
sidx_sort_guint (gconstpointer a, gconstpointer b)
{
  guint *ia = (guint *) a;
  guint *ib = (guint *) b;
  return (*ia > *ib);
}

static gint
sort_undo_files (gconstpointer a, gconstpointer b)
{
  UndoFile *ua = (UndoFile *) a;
  UndoFile *ub = (UndoFile *) b;
  return strcmp (ua->name, ub->name);
}

/* Return FALSE if executing a filesystem action fails.  */
static gboolean
undo_hist_motion (JFMContext *context, gboolean redo)
{
  UndoHist *elem;
  GArray *ctxs = g_undoctx.contexes;
  unsigned i;
  if (!redo && g_undoctx.pos <= 0)
    return TRUE;
  if (redo && g_undoctx.pos >= g_undoctx.stack->len)
    return TRUE;

  if (!redo)
    {
      /* First decrement, then undo the action at the current
	 position.  */
      g_undoctx.pos--;
      elem = &g_array_index (g_undoctx.stack, UndoHist,
			     g_undoctx.pos);
    }
  else
    {
      /* First do the action at the current position, then
	 increment (see later).  */
      elem = &g_array_index (g_undoctx.stack, UndoHist,
			     g_undoctx.pos);
    }

  /* Execute the action in the real or virtual filesystem.  If this
     fails, then return FALSE and do not update the GUI.  */
  switch (elem->oper)
    {
    case JFM_RENAME:
      {
	gchar *old_name = g_filename_from_utf8
	  (elem->t.rename.name, -1, NULL, NULL, NULL);
	gchar *new_name = g_filename_from_utf8
	  (elem->t.rename.new_name, -1, NULL, NULL, NULL);
	gchar *old_path = g_build_filename (elem->parent, old_name, NULL);
	gchar *new_path = g_build_filename (elem->parent, new_name, NULL);
	int result;
	if (!redo)
	  {
	    /* TODO FIXME: Prompt for overwrites.  */
	    struct stat sbuf;
	    if (stat (old_path, &sbuf) == 0)
	      result = -1;
	    else
	      result = rename (new_path, old_path);
	  }
	else
	  {
	    /* TODO FIXME: Prompt for overwrites.  */
	    struct stat sbuf;
	    if (stat (new_path, &sbuf) == 0)
	      result = -1;
	    else
	      result = rename (old_path, new_path);
	  }
	g_free (old_name);
	g_free (new_name);
	g_free (old_path);
	g_free (new_path);
	if (result == -1)
	  goto fail;
	break;
      }
    case JFM_MOVE:
      for (i = 0; i < elem->t.move.files->len; i++)
	{
	  UndoFile *file = &g_array_index (elem->t.move.files, UndoFile, i);
	  gchar *name = g_filename_from_utf8
	    (file->name, -1, NULL, NULL, NULL);
	  gchar *old_path = g_build_filename (elem->parent, name, NULL);
	  gchar *new_path = g_build_filename (elem->t.move.new_parent,
					      name, NULL);
	  int result;
	  if (!redo)
	    {
	      /* TODO FIXME: Prompt for overwrites.  */
	      struct stat sbuf;
	      if (file->lazy)
		fill_lazy_stats (elem->t.move.new_parent, file);
	      if (stat (old_path, &sbuf) == 0)
		result = -1;
	      else
		result = rename (new_path, old_path);
	    }
	  else
	    {
	      /* TODO FIXME: Prompt for overwrites.  */
	      struct stat sbuf;
	      if (stat (new_path, &sbuf) == 0)
		result = -1;
	      else
		result = rename (old_path, new_path);
	    }
	  g_free (name);
	  g_free (old_path);
	  g_free (new_path);
	  if (result == -1)
	    {
	      /* TODO FIXME */
	      /* Ut oh, this is tricky.  We're left in an irreversible
		 state if this happens.  */
	      /* If we undo our changes so far right away, we can keep
		 the state before the irreversible action
		 consistent.  */
	      goto fail;
	    }
	}
      break;
    case JFM_CREATE:
    case JFM_DELETE:
      for (i = 0; i < elem->t.create.files->len; i++)
	{
	  UndoFile *file = &g_array_index (elem->t.create.files, UndoFile, i);
	  gchar *name = g_filename_from_utf8
	    (file->name, -1, NULL, NULL, NULL);
	  gchar *path = g_build_filename (elem->parent, name, NULL);
	  int result;
	  if (file->lazy)
	    fill_lazy_stats (elem->parent, file);
	  /* We only support undo/redo of empty directory/file
	     creates/deletes on real filesystems.  */
	  if (g_array_index (elem->t.create.files, UndoFile,
			     i).fsnode != NULL)
	    result = -1;
	  else if (g_array_index (elem->t.create.files,
				  UndoFile, i).is_dir)
	    {
	      if (!redo && elem->oper == JFM_DELETE ||
		  redo && elem->oper == JFM_CREATE)
		  result = mkdir (path,
				  S_IRUSR | S_IWUSR | S_IXUSR |
				  S_IRGRP | S_IWGRP | S_IXGRP |
				  S_IROTH | S_IWOTH | S_IXOTH);
	      else /* (!redo && elem->oper == JFM_CREATE ||
		      redo && elem->oper == JFM_DELETE) */
		result = rmdir (path);
	    }
	  else
	    {
	      if (!redo && elem->oper == JFM_DELETE ||
		  redo && elem->oper == JFM_CREATE)
		{
		  int fd = creat (path,
				  S_IRUSR | S_IWUSR |
				  S_IRGRP | S_IWGRP |
				  S_IROTH | S_IWOTH);
		  if (fd == -1)
		    result = -1;
		  else
		    {
		      result = 0;
		      close (fd);
		    }
		}
	      else /* (!redo && elem->oper == JFM_CREATE ||
		      redo && elem->oper == JFM_DELETE) */
		{
		  struct stat64 sbuf;
		  lstat64 (path, &sbuf);
		  if (!S_ISREG (sbuf.st_mode) || sbuf.st_size != 0)
		    result = -1;
		  else
		    result = unlink (path);
		}
	    }
	  g_free (name);
	  g_free (path);
	  if (result == -1)
	    {
	      /* TODO FIXME */
	      /* Ut oh, this is tricky.  We're left in an irreversible
		 state if this happens.  */
	      /* If we undo our changes so far right away, we can keep
		 the state before the irreversible action
		 consistent.  */
	      goto fail;
	    }
	}
      break;
    }

  /* We need to execute the action across all visible window contexes,
     since there may be more than one window viewing the same
     directory.  */
  /* Now, this is tricky.

     * Undo create is the same as redo delete.
     * Undo delete is the same as redo create.
     * Undo move out of current directory is the same as "create" in
       the current directory.
     * Undo move into the current directory is the same as "delete" in
       the current directory.
     * Redo move out of the current directory is the same as "delete"
       in the current directory.
     * Redo move into the current directory is the same as "create" in
       the current directory.

     Thus, we fold these cases together.

     Two other cases to watch out for:

     * When creating a folder, the action is already performed in the
       current context.
     * When renaming a file, the action is already performed in the
       current context.
  */
  for (i = 0; i < ctxs->len; i++)
    {
      JFMContext *curctx = g_array_index (ctxs, JFMContext_ptr, i);
      gint sort_column_id;
      GtkSortType sort_order;
      if (context->finishing_rename && curctx == context)
	continue;
      /* Disable sorting so that we don't get a performance hit.  */
      gtk_tree_sortable_get_sort_column_id
	(GTK_TREE_SORTABLE (curctx->store),
	 &sort_column_id,
	 &sort_order);
      gtk_tree_sortable_set_sort_column_id
	(GTK_TREE_SORTABLE (curctx->store),
	 GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
	 GTK_SORT_ASCENDING);
      if ((!strcmp (elem->parent, curctx->parent) &&
	   (!redo && elem->oper == JFM_CREATE ||
	    redo && elem->oper == JFM_DELETE ||
	    redo && elem->oper == JFM_MOVE)) ||
	  (!redo && elem->oper == JFM_MOVE &&
	   !strcmp (elem->t.move.new_parent, curctx->parent)))
	{
	  /* Do a delete.  */
	  unsigned i, j;
	  GArray *sname_array = g_array_new
	    (FALSE, FALSE, sizeof (StoreIndex));
	  GArray *sidx_array = g_array_new
	    (FALSE, FALSE, sizeof (guint));

	  /* TODO FIXME: We should do this once in advance.  Sort the
	     file list.  */
	  g_array_sort (elem->t.delete.files, sort_undo_files);

	  /* Sort the list store.  */
	  /* TODO FIXME: We would like to use this code path because
	     it is a bit more elegant, but this causes us trouble
	     because it doesn't use the same sorting algorithm.
	     Hence, we leave this code commented out for now and use
	     the other code path.  The performance is essentially the
	     same, though.  */
	  /* gtk_tree_sortable_set_sort_column_id
	    (GTK_TREE_SORTABLE (curctx->store),
	     COL_DISPLAY_NAME,
	     GTK_SORT_ASCENDING);

	  { /\* Walk the sorted lists and delete the corresponding
	       files.  *\/
	      GtkTreeIter iter;
	      gboolean do_next;
	      do_next = gtk_tree_model_get_iter_first
		(GTK_TREE_MODEL (curctx->store), &iter);
	      for (i = 0; i < elem->t.delete.files->len && do_next; )
		{
		  gchar *name = g_array_index
		    (elem->t.delete.files, UndoFile, i).name;
		  gchar *display_name;
		  int diff;
		  gtk_tree_model_get
		    (GTK_TREE_MODEL (curctx->store),
		     &iter, COL_DISPLAY_NAME, &display_name, -1);
		  diff = strcmp (name, display_name);
		  if (!diff)
		    {
		      gtk_list_store_remove (curctx->store, &iter);
		      i++;
		    }
		  else if (diff < 0)
		    {
		      /\* Force a continue when the desired name does
			 not exist.  Also, we need to stay on the same
			 tree iterator when this happens.  *\/
		      i++;
		    }
		  else /\* (diff > 0) *\/
		    do_next = gtk_tree_model_iter_next
		      (GTK_TREE_MODEL (curctx->store), &iter);
		}
	  } */

	  { /* First create a sorted copy of the files in the
	       directory.  */
	      GtkTreeIter iter;
	      gboolean do_next;
	      i = 0;
	      do_next = gtk_tree_model_get_iter_first
	  	(GTK_TREE_MODEL (curctx->store), &iter);
	      while (do_next)
	  	{
	  	  StoreIndex sidx;
	  	  gtk_tree_model_get
	  	    (GTK_TREE_MODEL (curctx->store),
	  	     &iter, COL_DISPLAY_NAME, &sidx.display_name, -1);
	  	  sidx.index = i;
	  	  g_array_append_val (sname_array, sidx);
	  	  do_next = gtk_tree_model_iter_next
	  	    (GTK_TREE_MODEL (curctx->store), &iter);
	  	  i++;
	  	}
	  }
	  g_array_sort (sname_array, sidx_sort_by_name);

	  /* Copy out the directory files that are to be deleted and
	     sort them by index.  */
	  for (i = 0, j = 0;
	       (i < elem->t.delete.files->len &&
	  	j < sname_array->len); j++)
	    {
	      gchar *name = g_array_index
	  	(elem->t.delete.files, UndoFile, i).name;
	      gchar *display_name = g_array_index
	  	(sname_array, StoreIndex, j).display_name;
	      int diff = strcmp (name, display_name);
	      if (!diff)
	  	{
	  	  guint index = g_array_index
	  	    (sname_array, StoreIndex, j).index;
	  	  g_array_append_val (sidx_array, index);
	  	  i++;
	  	}
	      else if (diff < 0)
	  	{
	  	  /* Force a continue when the desired name does not
	  	     exist.  Also, we need to stay on the same `j'
	  	     when this happens.  */
	  	  i++; j--;
	  	}
	      g_free (display_name);
	    }
	  g_array_free (sname_array, TRUE);
	  g_array_sort (sidx_array, sidx_sort_guint);

	  { /* Delete the files.  */
	    GtkTreeIter iter;
	    unsigned num_deletes = 0;
	    /* We're being tricky here.  Each delete offsets our
	       actual indexes by one, but if we keep incrementing
	       anyways, we will compensate properly for the matching
	       to work.  */
	    for (i = 0; i < sidx_array->len; i++)
	      {
	  	guint index =
	  	  g_array_index (sidx_array, guint, i);
	  	gtk_tree_model_iter_nth_child
	  	  (GTK_TREE_MODEL (curctx->store), &iter, NULL,
	  	   index - num_deletes);
	  	gtk_list_store_remove (curctx->store, &iter);
	  	num_deletes++;
	      }
	    g_array_free (sidx_array, TRUE);
	  }
	}
      else if ((!strcmp (elem->parent, curctx->parent) &&
		(!redo && elem->oper == JFM_DELETE ||
		 redo && elem->oper == JFM_CREATE ||
		 !redo && elem->oper == JFM_MOVE)) ||
	       (redo && elem->oper == JFM_MOVE &&
		!strcmp (elem->t.move.new_parent, curctx->parent)))
	{
	  /* Do a create.  */
	  unsigned i;
	  for (i = 0; i < elem->t.create.files->len; i++)
	    {
	      GdkPixbuf *icon_pixbuf;
	      UndoFile *file = &g_array_index
		(elem->t.create.files, UndoFile, i);
	      GtkTreeIter iter;
	      gchar *fs_name = g_filename_from_utf8
		(file->name, -1, NULL, NULL, NULL);
	      if (file->is_dir)
		{
		  if (curctx->view_mode == JFM_VIEW_ICONS)
		    icon_pixbuf = folder_pixbuf;
		  else
		    icon_pixbuf = folder_sm_pixbuf;
		}
	      else
		{
		  if (curctx->view_mode == JFM_VIEW_ICONS)
		    icon_pixbuf = file_pixbuf;
		  else
		    icon_pixbuf = file_sm_pixbuf;
		}
	      gtk_list_store_insert_with_values
		(curctx->store, &iter, 0,
		 COL_FS_NAME, fs_name,
		 COL_DISPLAY_NAME, file->name,
		 COL_PIXBUF, icon_pixbuf,
		 COL_IS_DIRECTORY, file->is_dir,
		 COL_SIZE, file->file_size,
		 COL_DISP_SIZE, file->disp_size,
		 COL_MTIME, file->disp_mtime,
		 -1);
	      g_free (fs_name);
	    }
	}
      else if (elem->oper == JFM_RENAME)
	{
	  if (!redo)
	    {
	      GtkTreeIter iter;
	      gboolean do_next;
	      do_next = gtk_tree_model_get_iter_first
		(GTK_TREE_MODEL (curctx->store), &iter);
	      while (do_next)
		{
		  gchar *display_name;
		  gtk_tree_model_get
		    (GTK_TREE_MODEL (curctx->store),
		     &iter, COL_DISPLAY_NAME, &display_name, -1);
		  if (!g_strcmp0 (display_name, elem->t.rename.new_name))
		    {
		      g_free (display_name);
		      /* Rename it!  */
		      gtk_list_store_set
			(GTK_LIST_STORE (curctx->store), &iter,
			 COL_DISPLAY_NAME, elem->t.rename.name, -1);
		      break;
		    }
		  else
		    g_free (display_name);
		  do_next = gtk_tree_model_iter_next
		    (GTK_TREE_MODEL (curctx->store), &iter);
		}
	    }
	  else
	    {
	      GtkTreeIter iter;
	      gboolean do_next;
	      do_next = gtk_tree_model_get_iter_first
		(GTK_TREE_MODEL (curctx->store), &iter);
	      while (do_next)
		{
		  gchar *display_name;
		  gtk_tree_model_get
		    (GTK_TREE_MODEL (curctx->store),
		     &iter, COL_DISPLAY_NAME, &display_name, -1);
		  if (!strcmp (display_name, elem->t.rename.name))
		    {
		      g_free (display_name);
		      /* Rename it!  */
		      gtk_list_store_set
			(GTK_LIST_STORE (curctx->store), &iter,
			 COL_DISPLAY_NAME, elem->t.rename.new_name, -1);
		      break;
		    }
		  else
		    g_free (display_name);
		  do_next = gtk_tree_model_iter_next
		    (GTK_TREE_MODEL (curctx->store), &iter);
		}
	    }
	}
      /* Enable sorting.  */
      gtk_tree_sortable_set_sort_column_id
	(GTK_TREE_SORTABLE (curctx->store),
	 sort_column_id,
	 sort_order);
    }

  if (redo) /* Now do the increment.  */
    g_undoctx.pos++;

  gtk_action_set_sensitive (g_undo_action,
			    (g_undoctx.pos <= 0) ? FALSE : TRUE);
  gtk_action_set_sensitive
    (g_redo_action,
     (g_undoctx.pos >= g_undoctx.stack->len)
     ? FALSE : TRUE);

  /* Update file modification information.  */
  if (g_undoctx.pos > 0)
    {
      file_modified = TRUE;
      file_autosaved = FALSE;
    }
  else /* (g_undoctx.pos == 0) */
    file_modified = FALSE;

  return TRUE;
 fail:
  if (!redo)
    g_undoctx.pos++; /* Undo our early decrement.  */

  {
    GtkWidget *dialog;
    GtkResponseType result;

    dialog = gtk_message_dialog_new (GTK_WINDOW (context->window),
				     GTK_DIALOG_DESTROY_WITH_PARENT,
				     GTK_MESSAGE_ERROR,
				     GTK_BUTTONS_CLOSE,
				     "%s",
				     _("Filesystem failure."));
    result = gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
  }
  return FALSE;
}

/* Return FALSE if executing a filesystem action fails.  */
/* Take note!  The view history stack includes the current view within
   it, but the undo history stack only includes past actions.  Thus,
   the stack disciplines differ by a single index position.  */
static gboolean
push_jfm_action (JFMContext *context, UndoHist action)
{
  /* Clear the redo history, if applicable.  */
  if (g_undoctx.stack->len > 0 &&
      g_undoctx.pos < g_undoctx.stack->len)
    {
      free_undo_entries (g_undoctx.stack,
			 g_undoctx.pos, FALSE);
      g_array_remove_range (g_undoctx.stack,
			    g_undoctx.pos,
			    g_undoctx.stack->len -
			      g_undoctx.pos);
      gtk_action_set_sensitive (g_redo_action, FALSE);
    }
  /* Add the history stack item for the new action.  */
  g_array_append_val (g_undoctx.stack, action);

  /* Execute the action in the GUI.  */
  if (!undo_hist_motion (context, TRUE))
    {
      /* Delete our newly-created action that failed.  Note that this
	 leaves us in a wonky state where we've cleared the redo stack
	 but haven't done anything.  In other words, this should be
	 avoided by higher-level application code when possible.  */
      free_undo_entries (g_undoctx.stack,
			 g_undoctx.pos + 1, FALSE);
      g_array_remove_range (g_undoctx.stack,
			    g_undoctx.pos,
			    g_undoctx.stack->len -
			      g_undoctx.pos);
      return FALSE;
    }

  /* Update file modification information.  */
  file_modified = TRUE;
  file_autosaved = FALSE;

  return TRUE;
}

static void
item_activated (GtkWidget   *icon_view,
		GtkTreePath *tree_path,
		gpointer     user_data)
{
  GtkListStore *store;
  gchar *fs_name;
  gchar *path;
  GtkTreeIter iter;
  gboolean is_dir;
  JFMContext *context = (JFMContext *) user_data;

  gtk_tree_model_get_iter (GTK_TREE_MODEL (context->store),
			   &iter, tree_path);
  gtk_tree_model_get (GTK_TREE_MODEL (context->store), &iter,
		      COL_FS_NAME, &fs_name,
		      COL_IS_DIRECTORY, &is_dir,
		      -1);

  if (!is_dir)
    {
      g_free (fs_name);
      return;
    }
  path = g_build_filename (context->parent, fs_name, NULL);
  g_free (fs_name);

  /* Replace parent with path and re-fill the model.  */
  push_directory (context, path);
}

static void
tree_item_activated (GtkTreeView *icon_view,
		     GtkTreePath *tree_path,
		     GtkTreeViewColumn *column,
		     gpointer     user_data)
{
  return item_activated (GTK_WIDGET (icon_view), tree_path, user_data);
}

static void
location_bar_activate (GtkEntry *entry,
		       gpointer user_data)
{
  const gchar *entry_name;
  gchar *dir_name;
  JFMContext *context = (JFMContext *) user_data;

  /* Make a copy of the string in the text entry.  */
  entry_name = gtk_entry_get_text (entry);
  dir_name = (gchar *) g_malloc (sizeof (gchar) * (strlen (entry_name) + 1));
  strcpy (dir_name, entry_name);

  context->parent = dir_name;
  push_directory (context, dir_name);

  gtk_widget_grab_focus (context->icon_view);
}

static void
selection_changed (GtkWidget *widget,
		   gpointer user_data)
{
  JFMContext *context = (JFMContext *) user_data;
  gboolean has_selection = FALSE;

  if (context->view_mode == JFM_VIEW_ICONS ||
      context->view_mode == JFM_VIEW_SM_ICONS)
    {
      GtkTreeIter iter;
      gboolean do_next;
      do_next = gtk_tree_model_get_iter_first
	(GTK_TREE_MODEL (context->store), &iter);
      while (!has_selection && do_next)
	{
	  GtkTreePath *path = gtk_tree_model_get_path
	    (GTK_TREE_MODEL (context->store), &iter);
	  if (gtk_icon_view_path_is_selected
	      (GTK_ICON_VIEW (context->icon_view), path))
	    has_selection = TRUE;
	  gtk_tree_path_free (path);
	  do_next = gtk_tree_model_iter_next
	    (GTK_TREE_MODEL (context->store), &iter);
	}
    }
  else if (context->view_mode == JFM_VIEW_DETAILS)
    {
      GtkTreeSelection *selection = gtk_tree_view_get_selection
	(GTK_TREE_VIEW (context->icon_view));
      gint num_selected;
      num_selected = gtk_tree_selection_count_selected_rows (selection);
      if (num_selected > 0)
	has_selection = TRUE;
    }

  gtk_action_set_sensitive (context->cut_action, has_selection);
  gtk_action_set_sensitive (context->rename_action, has_selection);
  if (!g_disable_delete)
    gtk_action_set_sensitive (context->delete_action, has_selection);
}

static void
close_window (GtkWidget *window, gpointer user_data)
{
  JFMContext *context = (JFMContext *) user_data;
  unsigned i;

  gtk_widget_destroy (window);
  for (i = 0; i < context->browshist_stack->len; i++)
    {
      gchar *parent = g_array_index (context->browshist_stack,
				     BrowseHist, i).parent;
      g_free (parent);
    }
  g_array_free (context->browshist_stack, TRUE);
  for (i = 0; i < g_undoctx.contexes->len; i++)
    {
      if (context == g_array_index (g_undoctx.contexes, JFMContext_ptr, i))
	{
	  g_array_remove_index (g_undoctx.contexes, i);
	  break;
	}
    }
  g_free (context);
  g_num_windows--;

  if (g_num_windows != 0)
    return;

  free_undo_entries (g_undoctx.stack, g_undoctx.pos, TRUE);
  g_array_free (g_undoctx.stack, TRUE);
  g_array_free (g_undoctx.contexes, TRUE);
  {
    GArray *temp_stack = g_array_new (FALSE, FALSE, sizeof (UndoHist));
    g_array_append_val (temp_stack, g_cut_buffer);
    free_undo_entries (temp_stack, 0, TRUE);
  }
  g_object_unref (file_pixbuf);
  file_pixbuf = NULL;
  g_object_unref (file_sm_pixbuf);
  file_sm_pixbuf = NULL;
  g_object_unref (folder_pixbuf);
  folder_pixbuf = NULL;
  g_object_unref (folder_sm_pixbuf);
  folder_sm_pixbuf = NULL;

  gtk_main_quit ();
}

static gboolean
delete_window (GtkWidget *window, GdkEvent *event, gpointer user_data)
{
  JFMContext *context = (JFMContext *) user_data;
  if (g_num_windows == 1 && !check_save (context, TRUE))
    return TRUE;
  return FALSE;
}

static GtkWidget *
create_jfm_window (gchar *view_dir,
		   JFMViewMode view_mode,
		   gboolean view_toolbar,
		   gboolean view_location_bar,
		   gboolean full_path_title);
static void
set_view_mode (JFMContext *context);

static void
activate_action (GtkAction *action,
		 gpointer user_data)
{
  const gchar *name = gtk_action_get_name (action);
  const gchar *typename = G_OBJECT_TYPE_NAME (action);
  JFMContext *context = (JFMContext *) user_data;
  if (!strcmp (name, "NewWindow"))
    {
      create_jfm_window (context->parent,
			 context->view_mode,
			 context->view_toolbar,
			 context->view_location_bar,
			 context->full_path_title);
    }
  else if (!strcmp (name, "Open"))
    {
      open_file (context);
    }
  else if (!strcmp (name, "Save"))
    {
      if (loaded_fname != NULL &&
	  save_sh_project (context, loaded_fname))
	{
	  file_modified = FALSE;
	  remove_autosave ();
	}
      else
	save_as (context);
    }
  else if (!strcmp (name, "SaveAs"))
    save_as (context);
  else if (!strcmp (name, "CloseWindow"))
    gtk_widget_destroy (context->window);
  else if (!strcmp (name, "Undo"))
    undo_hist_motion (context, FALSE);
  else if (!strcmp (name, "Redo"))
    undo_hist_motion (context, TRUE);
  else if (!strcmp (name, "NewFolder") || !strcmp (name, "NewFile"))
    {
      gboolean is_dir = !strcmp (name, "NewFolder") ? TRUE : FALSE;
      const gchar *name = is_dir ? _("New Folder") : _("New File");
      gint64 file_size = 0;
      GdkPixbuf *icon_pixbuf;
      GtkTreeIter iter;
      GtkTreePath *path;
      if (context->view_mode == JFM_VIEW_ICONS)
	icon_pixbuf = is_dir ? folder_pixbuf : file_pixbuf;
      else
	icon_pixbuf = is_dir ? folder_sm_pixbuf : file_sm_pixbuf;
      gtk_list_store_insert_with_values
	(context->store, &iter, 0,
	 COL_FS_NAME, name,
	 COL_DISPLAY_NAME, name,
	 COL_PIXBUF, icon_pixbuf,
	 COL_IS_DIRECTORY, is_dir,
	 COL_SIZE, file_size,
	 COL_DISP_SIZE, "",
	 COL_MTIME, "",
	 -1);
      path = gtk_tree_model_get_path
	(GTK_TREE_MODEL (context->store), &iter);
      g_object_set (context->file_cell,
		    "editable", TRUE,
		    NULL);
      if (context->view_mode == JFM_VIEW_ICONS ||
	  context->view_mode == JFM_VIEW_SM_ICONS)
	{
	  /* Again, another hack, this time because the Icon View
	     widget needs some signal cycle between creating a new
	     folder and activating editing on it.  */
	  context->temp_path = path;
	  g_idle_add (finish_new_folder, context);
	}
      else if (context->view_mode == JFM_VIEW_DETAILS)
	{
	  gtk_tree_view_set_cursor (GTK_TREE_VIEW (context->icon_view),
				    path, context->file_column, TRUE);
	  gtk_tree_path_free (path);
	}
      context->creating_file = TRUE;
    }
  else if (!strcmp (name, "Cut"))
    {
      GList *items, *cur;
      if (context->view_mode == JFM_VIEW_ICONS ||
	  context->view_mode == JFM_VIEW_SM_ICONS)
	items = gtk_icon_view_get_selected_items
	  (GTK_ICON_VIEW (context->icon_view));
      else if (context->view_mode == JFM_VIEW_DETAILS)
	{
	  GtkTreeSelection *selection = gtk_tree_view_get_selection
	    (GTK_TREE_VIEW (context->icon_view));
	  items = gtk_tree_selection_get_selected_rows (selection, NULL);
	}
      /* Store the list of files to move in the cut buffer.  */
      g_cut_buffer.oper = JFM_MOVE;
      g_free (g_cut_buffer.parent);
      g_cut_buffer.parent = g_strdup (context->parent);
      g_array_free (g_cut_buffer.t.move.files, TRUE);
      g_cut_buffer.t.move.files =
	g_array_new (FALSE, FALSE, sizeof (UndoFile));
      cur = items;
      while (cur != NULL)
	{
	  GtkTreePath *path = (GtkTreePath *) cur->data;
	  GtkTreeIter iter;
	  UndoFile file;
	  gtk_tree_model_get_iter (GTK_TREE_MODEL (context->store),
				   &iter, path);
	  file.lazy = FALSE;
	  gtk_tree_model_get (GTK_TREE_MODEL (context->store), &iter,
			      COL_DISPLAY_NAME, &file.name,
			      COL_IS_DIRECTORY, &file.is_dir,
			      COL_SIZE, &file.file_size,
			      COL_DISP_SIZE, &file.disp_size,
			      COL_MTIME, &file.disp_mtime,
			      -1);
	  file.fsnode = NULL;
	  g_array_append_val (g_cut_buffer.t.move.files, file);
	  gtk_tree_path_free (path);
	  cur = cur->next;
	}
      g_list_free (items);
      /* Now we need to change the paste sensitivity across all
	 windows.  */
      gtk_action_set_sensitive (g_paste_action, TRUE);
    }
  /* else if (!strcmp (name, "Copy"))
    ; */
  else if (!strcmp (name, "Paste"))
    {
      /* Do not move to the same location.  */
      if (!strcmp (g_cut_buffer.parent, context->parent))
	return;
      g_cut_buffer.t.move.new_parent = g_strdup (context->parent);
      push_jfm_action (context, g_cut_buffer);
      /* Now that we've tranferred the cut buffer to an undo action,
	 we need to re-initialize it.  */
      g_cut_buffer.parent = NULL;
      g_cut_buffer.t.move.files =
	g_array_new (FALSE, FALSE, sizeof (UndoFile));
      g_cut_buffer.t.move.new_parent = NULL;
      /* Now we need to change the paste sensitivity across all
	 windows.  */
      gtk_action_set_sensitive (g_paste_action, FALSE);
    }
  else if (!strcmp (name, "Rename"))
    {
      if (context->view_mode == JFM_VIEW_ICONS ||
	  context->view_mode == JFM_VIEW_SM_ICONS)
	{
	  GtkTreePath *path;
	  GtkCellRenderer *cell;
	  gtk_icon_view_get_cursor (GTK_ICON_VIEW (context->icon_view),
				    &path, &cell);
	  if (path)
	    {
	      g_object_set (context->file_cell,
			    "editable", TRUE,
			    NULL);
	      gtk_icon_view_set_cursor (GTK_ICON_VIEW (context->icon_view),
					path, context->file_cell, TRUE);
	      gtk_tree_path_free (path);
	    }
	}
      else if (context->view_mode == JFM_VIEW_DETAILS)
	{
	  GtkTreePath *path;
	  GtkTreeViewColumn *focus_column;
	  gtk_tree_view_get_cursor (GTK_TREE_VIEW (context->icon_view),
				    &path, &focus_column);
	  if (path)
	    {
	      g_object_set (context->file_cell,
			    "editable", TRUE,
			    NULL);
	      gtk_tree_view_set_cursor (GTK_TREE_VIEW (context->icon_view),
					path, context->file_column, TRUE);
	      gtk_tree_path_free (path);
	    }
	}
      /* else error */
    }
  else if (!strcmp (name, "Delete"))
    {
      UndoHist action;
      GList *items, *cur;
      if (context->view_mode == JFM_VIEW_ICONS ||
	  context->view_mode == JFM_VIEW_SM_ICONS)
	items = gtk_icon_view_get_selected_items
	  (GTK_ICON_VIEW (context->icon_view));
      else if (context->view_mode == JFM_VIEW_DETAILS)
	{
	  GtkTreeSelection *selection = gtk_tree_view_get_selection
	    (GTK_TREE_VIEW (context->icon_view));
	  items = gtk_tree_selection_get_selected_rows (selection, NULL);
	}
      /* Create the list of files to delete for the undo history, then
	 execute the deletion when we push the new undo action.  (We
	 need to do things this way so that the action gets executed
	 across all windows.)  */
      action.oper = JFM_DELETE;
      action.parent = g_strdup (context->parent);
      action.t.delete.files = g_array_new (FALSE, FALSE, sizeof (UndoFile));
      cur = items;
      while (cur != NULL)
	{
	  GtkTreePath *path = (GtkTreePath *) cur->data;
	  GtkTreeIter iter;
	  UndoFile file;
	  gtk_tree_model_get_iter (GTK_TREE_MODEL (context->store),
				   &iter, path);
	  file.lazy = FALSE;
	  gtk_tree_model_get (GTK_TREE_MODEL (context->store), &iter,
			      COL_DISPLAY_NAME, &file.name,
			      COL_IS_DIRECTORY, &file.is_dir,
			      COL_SIZE, &file.file_size,
			      COL_DISP_SIZE, &file.disp_size,
			      COL_MTIME, &file.disp_mtime,
			      -1);
	  file.fsnode = NULL;
	  g_array_append_val (action.t.delete.files, file);
	  gtk_tree_path_free (path);
	  cur = cur->next;
	}
      g_list_free (items);
      push_jfm_action (context, action);
    }
  else if (!strcmp (name, "SelectAll"))
    {
      if (context->view_mode == JFM_VIEW_ICONS ||
	  context->view_mode == JFM_VIEW_SM_ICONS)
	gtk_icon_view_select_all (GTK_ICON_VIEW (context->icon_view));
      else if (context->view_mode == JFM_VIEW_DETAILS)
	{
	  GtkTreeSelection *selection = gtk_tree_view_get_selection
	    (GTK_TREE_VIEW (context->icon_view));
	  gtk_tree_selection_select_all (selection);
	}
    }
  else if (!strcmp (name, "InvertSelection"))
    {
      if (context->view_mode == JFM_VIEW_ICONS ||
	  context->view_mode == JFM_VIEW_SM_ICONS)
	{
	  GtkTreeIter iter;
	  gboolean do_next;
	  do_next = gtk_tree_model_get_iter_first
	    (GTK_TREE_MODEL (context->store), &iter);
	  while (do_next)
	    {
	      GtkTreePath *path = gtk_tree_model_get_path
		(GTK_TREE_MODEL (context->store), &iter);
	      if (gtk_icon_view_path_is_selected
		  (GTK_ICON_VIEW (context->icon_view), path))
		gtk_icon_view_unselect_path
		  (GTK_ICON_VIEW (context->icon_view), path);
	      else
		gtk_icon_view_select_path
		  (GTK_ICON_VIEW (context->icon_view), path);
	      gtk_tree_path_free (path);
	      do_next = gtk_tree_model_iter_next
		(GTK_TREE_MODEL (context->store), &iter);
	    }
	}
      else if (context->view_mode == JFM_VIEW_DETAILS)
	{
	  GtkTreeSelection *selection = gtk_tree_view_get_selection
	    (GTK_TREE_VIEW (context->icon_view));
	  GtkTreeIter iter;
	  gboolean do_next;
	  do_next = gtk_tree_model_get_iter_first
	    (GTK_TREE_MODEL (context->store), &iter);
	  while (do_next)
	    {
	      if (gtk_tree_selection_iter_is_selected (selection, &iter))
		gtk_tree_selection_unselect_iter (selection, &iter);
	      else
		gtk_tree_selection_select_iter (selection, &iter);
	      do_next = gtk_tree_model_iter_next
		(GTK_TREE_MODEL (context->store), &iter);
	    }
	}
    }
  else if (!strcmp (name, "Back"))
    browshist_motion (context, FALSE);
  else if (!strcmp (name, "Forward"))
    browshist_motion (context, TRUE);
  else if (!strcmp (name, "Up"))
    {
      gchar *dir_name = g_path_get_dirname (context->parent);
      push_directory (context, dir_name);
    }
  else if (!strcmp (name, "Home"))
    push_directory (context, g_strdup (g_get_home_dir ()));
  else if (!strcmp (name, "AddressBar"))
    {
      g_signal_emit_by_name (context->location_bar, "activate");
      gtk_widget_grab_focus (context->location_bar);
    }
  else if (!strcmp (name, "HideDotFiles"))
    {
      GArray *ctxs = g_undoctx.contexes;
      unsigned i;
      g_hide_dotfiles = !g_hide_dotfiles;
      /* Now we need to update all visible windows with this new
	 setting.  */
      for (i = 0; i < ctxs->len; i++)
	{
	  JFMContext *curctx = g_array_index (ctxs, JFMContext_ptr, i);
	  set_view_mode (curctx);
	}
    }
  else if (!strcmp (name, "ViewMenuBar"))
    {
      context->view_menubar = !context->view_menubar;
      if (context->view_menubar)
	gtk_widget_show (context->menu_bar);
      else
	gtk_widget_hide (context->menu_bar);
    }
  else if (!strcmp (name, "ViewToolBar"))
    {
      context->view_toolbar = !context->view_toolbar;
      if (context->view_toolbar)
	gtk_widget_show (context->tool_bar);
      else
	gtk_widget_hide (context->tool_bar);
    }
  else if (!strcmp (name, "ViewLocationBar"))
    {
      context->view_location_bar = !context->view_location_bar;
      if (context->view_location_bar)
	gtk_widget_show (context->location_bar);
      else
	gtk_widget_hide (context->location_bar);
    }
  else if (!strcmp (name, "FullPathTitle"))
    {
      context->full_path_title = !context->full_path_title;
      set_view_mode (context);
    }
  else if (!strcmp (name, "About"))
    display_about_box (context->window);
}

static void
activate_radio_action (GtkAction *action, GtkRadioAction *current,
		       gpointer user_data)
{
  const gchar *name = gtk_action_get_name (GTK_ACTION (current));
  const gchar *typename = G_OBJECT_TYPE_NAME (GTK_ACTION (current));
  JFMContext *context = (JFMContext *) user_data;
  if (!strcmp (name, "Icons"))
    {
      context->view_mode = JFM_VIEW_ICONS;
      set_view_mode (context);
    }
  else if (!strcmp (name, "SmallIcons"))
    {
      context->view_mode = JFM_VIEW_SM_ICONS;
      set_view_mode (context);
    }
  else if (!strcmp (name, "Details"))
    {
      context->view_mode = JFM_VIEW_DETAILS;
      set_view_mode (context);
    }
}

static void
cancel_rename (GtkCellRenderer *cell,
	       gpointer user_data)
{
  JFMContext *context = (JFMContext *) user_data;
  GtkTreeModel *model = (GtkTreeModel *) context->store;
  GtkTreePath *path;
  GtkTreeIter iter;

  /* TODO FIXME: If create cancel fails and a dialog box needs to be
     displayed while the user is clicking and dragging, our software
     will crash and burn.  This is a bug with GTK+.  */

  if (context->view_mode == JFM_VIEW_ICONS ||
      context->view_mode == JFM_VIEW_SM_ICONS)
    {
      GtkCellRenderer *recv_cell;
      gtk_icon_view_get_cursor (GTK_ICON_VIEW (context->icon_view),
				&path, &recv_cell);
    }
  else if (context->view_mode == JFM_VIEW_DETAILS)
    {
      GtkTreeViewColumn *focus_column;
      gtk_tree_view_get_cursor (GTK_TREE_VIEW (context->icon_view),
				&path, &focus_column);
    }
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_path_free (path);

  context->finishing_rename = TRUE;

  if (context->creating_file)
    {
      UndoHist action;
      UndoFile file;
      gboolean act_success;
      action.oper = JFM_CREATE;
      action.parent = g_strdup (context->parent);
      action.t.create.files = g_array_new
	(FALSE, FALSE, sizeof (UndoFile));
      file.lazy = FALSE;
      gtk_tree_model_get (model, &iter,
			  COL_DISPLAY_NAME, &file.name,
			  COL_IS_DIRECTORY, &file.is_dir,
			  COL_SIZE, &file.file_size,
			  COL_DISP_SIZE, &file.disp_size,
			  COL_MTIME, &file.disp_mtime,
			  -1);
      file.fsnode = NULL;
      g_array_append_val (action.t.create.files, file);
      act_success = push_jfm_action (context, action);
      if (!act_success)
	{
	  /* Delete the file from the GUI since the system
	     action failed.  */
	  gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
	}
      else
	{
	    gchar *fs_name = g_filename_from_utf8
	      (file.name, -1, NULL, NULL, NULL);
	    gtk_list_store_set (GTK_LIST_STORE (model), &iter, COL_FS_NAME,
				fs_name, -1);
	    g_free (fs_name);
	}
      context->creating_file = FALSE;
    }

  context->finishing_rename = FALSE;
  g_object_set (cell,
		"editable", FALSE,
		NULL);
}

static void
finish_rename (GtkCellRendererText *cell,
	       const gchar         *path_string,
	       const gchar         *new_text,
	       gpointer             user_data)
{
  JFMContext *context = (JFMContext *) user_data;
  GtkTreeModel *model = (GtkTreeModel *) context->store;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
  GtkTreeIter iter;

  gint column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cell), "column"));

  context->finishing_rename = TRUE;

  gtk_tree_model_get_iter (model, &iter, path);

  switch (column)
    {
    case COL_DISPLAY_NAME:
      {
	gboolean act_success = FALSE;
	if (context->creating_file)
	  {
	    UndoHist action;
	    UndoFile file;
	    action.oper = JFM_CREATE;
	    action.parent = g_strdup (context->parent);
	    action.t.create.files = g_array_new
	      (FALSE, FALSE, sizeof (UndoFile));
	    file.name = g_strdup (new_text);
	    file.lazy = FALSE;
	    gtk_tree_model_get (model, &iter,
				COL_IS_DIRECTORY, &file.is_dir,
				COL_SIZE, &file.file_size,
				COL_DISP_SIZE, &file.disp_size,
				COL_MTIME, &file.disp_mtime,
				-1);
	    file.fsnode = NULL;
	    g_array_append_val (action.t.create.files, file);
	    act_success = push_jfm_action (context, action);
	    if (!act_success)
	      {
		/* Delete the file from the GUI since the system
		   action failed.  */
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
	      }
	    context->creating_file = FALSE;
	  }
	else
	  {
	    UndoHist action;
	    action.oper = JFM_RENAME;
	    gtk_tree_model_get (model, &iter,
				COL_DISPLAY_NAME, &action.t.rename.name,
				-1);
	    if (strcmp (action.t.rename.name, new_text))
	      {
		action.parent = g_strdup (context->parent);
		action.t.rename.new_name = g_strdup (new_text);
		act_success = push_jfm_action (context, action);
	      }
	    else
	      g_free (action.t.rename.name);
	  }

	if (act_success)
	  {
	    gchar *fs_name = g_filename_from_utf8
	      (new_text, -1, NULL, NULL, NULL);
	    gtk_list_store_set (GTK_LIST_STORE (model), &iter, column,
				new_text, -1);
	    gtk_list_store_set (GTK_LIST_STORE (model), &iter, COL_FS_NAME,
				fs_name, -1);
	    g_free (fs_name);
	  }
      }
      break;
    }
  context->finishing_rename = FALSE;
  g_object_set (cell,
		"editable", FALSE,
		NULL);

  gtk_tree_path_free (path);
}

static void
drag_begin (GtkWidget *widget, GdkDragContext *dc, gpointer user_data)
{
  JFMContext *context = (JFMContext *) user_data;
  g_drag_src = context;
}

static void
drag_end (GtkWidget *widget, GdkDragContext *dc, gpointer user_data)
{
  g_drag_src = NULL;
}

static void
drag_data_recv (GtkWidget *widget, GdkDragContext *dc,
		gint x, gint y, GtkSelectionData *selection_data,
		guint info, guint t, gpointer user_data)
{
  /* Move the selected files.  */
  JFMContext *context = (JFMContext *) user_data;
  UndoHist action;
  GList *items, *cur;
  if (g_drag_src->view_mode == JFM_VIEW_ICONS ||
      g_drag_src->view_mode == JFM_VIEW_SM_ICONS)
    items = gtk_icon_view_get_selected_items
      (GTK_ICON_VIEW (g_drag_src->icon_view));
  else if (g_drag_src->view_mode == JFM_VIEW_DETAILS)
    {
      GtkTreeSelection *selection = gtk_tree_view_get_selection
	(GTK_TREE_VIEW (g_drag_src->icon_view));
      items = gtk_tree_selection_get_selected_rows (selection, NULL);
    }
  /* Store the list of files to move in the cut buffer.  */
  action.oper = JFM_MOVE;
  action.parent = g_strdup (g_drag_src->parent);
  action.t.move.files =
    g_array_new (FALSE, FALSE, sizeof (UndoFile));
  cur = items;
  while (cur != NULL)
    {
      GtkTreePath *path = (GtkTreePath *) cur->data;
      GtkTreeIter iter;
      UndoFile file;
      gtk_tree_model_get_iter (GTK_TREE_MODEL (g_drag_src->store),
			       &iter, path);
      file.lazy = FALSE;
      gtk_tree_model_get (GTK_TREE_MODEL (g_drag_src->store), &iter,
			  COL_DISPLAY_NAME, &file.name,
			  COL_IS_DIRECTORY, &file.is_dir,
			  COL_SIZE, &file.file_size,
			  COL_DISP_SIZE, &file.disp_size,
			  COL_MTIME, &file.disp_mtime,
			  -1);
      file.fsnode = NULL;
      g_array_append_val (action.t.move.files, file);
      gtk_tree_path_free (path);
      cur = cur->next;
    }
  g_list_free (items);

  /* Do not move to the same location.  */
  if (!strcmp (action.parent, context->parent))
    return;
  action.t.move.new_parent = g_strdup (context->parent);
  push_jfm_action (context, action);
}

static void
set_view_mode (JFMContext *context)
{
  GtkWidget *icon_view;
  if (context->icon_view != NULL)
    {
      g_object_ref (context->store);
      gtk_widget_destroy (context->icon_view);
      context->icon_view = NULL;
    }

  /* Note that we must also refill the list store to pickup the new
     icon sizes.  */
  fill_store (context);

  if (context->view_mode == JFM_VIEW_ICONS ||
      context->view_mode == JFM_VIEW_SM_ICONS)
    {
      GtkOrientation icon_orient =
	(context->view_mode == JFM_VIEW_ICONS)
	? GTK_ORIENTATION_VERTICAL
	: GTK_ORIENTATION_HORIZONTAL;
      gint item_width =
	(context->view_mode == JFM_VIEW_ICONS) ? 80 : 120;
      GtkTargetEntry target_entry;
      icon_view = gtk_icon_view_new_with_model
	(GTK_TREE_MODEL (context->store));
      context->icon_view = icon_view;
      gtk_icon_view_set_selection_mode (GTK_ICON_VIEW (icon_view),
					GTK_SELECTION_MULTIPLE);
      g_object_unref (context->store);
      gtk_icon_view_set_item_width (GTK_ICON_VIEW (icon_view), item_width);
      gtk_icon_view_set_orientation (GTK_ICON_VIEW (icon_view),
				     icon_orient);
      if (context->view_mode == JFM_VIEW_SM_ICONS)
	{
	  gtk_icon_view_set_item_padding (GTK_ICON_VIEW (icon_view), 0);
	  gtk_icon_view_set_row_spacing (GTK_ICON_VIEW (icon_view), 0);
	  gtk_icon_view_set_column_spacing (GTK_ICON_VIEW (icon_view), 0);
	}

      /* Enable drag-and-drop.  */
      target_entry.target = "file";
      target_entry.flags = GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET;
      target_entry.info = 4839;
      gtk_icon_view_enable_model_drag_dest (GTK_ICON_VIEW (icon_view),
					    &target_entry,
					    1,
					    GDK_ACTION_MOVE);
      gtk_icon_view_enable_model_drag_source (GTK_ICON_VIEW (icon_view),
					      GDK_BUTTON1_MASK,
					      &target_entry,
					      1,
					      GDK_ACTION_MOVE);
      g_signal_connect (icon_view, "drag-begin",
			G_CALLBACK (drag_begin), context);
      g_signal_connect (icon_view, "drag-end",
			G_CALLBACK (drag_end), context);
      g_signal_connect (icon_view, "drag-data-received",
			G_CALLBACK (drag_data_recv), context);

      /* We now set which model columns that correspond to the
       * text and pixbuf of each item
       */
      gtk_icon_view_set_text_column (GTK_ICON_VIEW (icon_view),
				     COL_DISPLAY_NAME);
      gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (icon_view),
				       COL_PIXBUF);

      { /* Get and save the text cell so that we can make it
	   editable.  */
	  GtkCellRenderer *cell = NULL;
	  GList *cells, *iter;
	  cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (icon_view));
	  iter = cells;
	  while (iter && !GTK_IS_CELL_RENDERER_TEXT (iter->data))
	    iter = iter->next;
	  cell = (GtkCellRenderer *) iter->data;
	  g_list_free (cells);
	  context->file_cell = cell;
	  g_signal_connect (cell, "edited",
			    G_CALLBACK (finish_rename), context);
	  g_signal_connect (cell, "editing-canceled",
			    G_CALLBACK (cancel_rename), context);
      }

      g_signal_connect (icon_view, "item-activated",
			G_CALLBACK (item_activated), context);
      g_signal_connect (icon_view, "selection-changed",
			G_CALLBACK (selection_changed), context);
    }
  else if (context->view_mode == JFM_VIEW_DETAILS)
    {
      GtkTargetEntry target_entry;
      GtkCellRenderer *cell;
      GtkCellRenderer *pbcell;
      GtkTreeViewColumn *column;
      GtkTreeSelection *selection;
      GtkTreeIter iter;
      icon_view = gtk_tree_view_new_with_model
	(GTK_TREE_MODEL (context->store));
      context->icon_view = icon_view;
      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (icon_view));
      gtk_tree_selection_set_mode (GTK_TREE_SELECTION (selection),
				   GTK_SELECTION_MULTIPLE);
      g_object_unref (context->store);

      /* Enable drag-and-drop.  */
      target_entry.target = "file";
      target_entry.flags = GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET;
      target_entry.info = 4839;
      gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (icon_view),
					    &target_entry,
					    1,
					    GDK_ACTION_MOVE);
      gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (icon_view),
					      GDK_BUTTON1_MASK,
					      &target_entry,
					      1,
					      GDK_ACTION_MOVE);
      g_signal_connect (icon_view, "drag-begin",
			G_CALLBACK (drag_begin), context);
      g_signal_connect (icon_view, "drag-end",
			G_CALLBACK (drag_end), context);
      g_signal_connect (icon_view, "drag-data-received",
			G_CALLBACK (drag_data_recv), context);

      /* Define the cell rendering for this list.  */
      column = gtk_tree_view_column_new ();
      context->file_column = column;
      /* We set to user-resizable fixed column widths because super
	 long file names stretching the file column can be
	 annoying.  */
      gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_resizable (column, TRUE);
      gtk_tree_view_column_set_min_width (column, 10);
      gtk_tree_view_column_set_fixed_width (column, 128);
      gtk_tree_view_column_set_sort_column_id (column, 0);
      pbcell = gtk_cell_renderer_pixbuf_new ();
      gtk_tree_view_column_pack_start (column, pbcell, FALSE);
      gtk_tree_view_column_set_attributes (column, pbcell,
					   "pixbuf", COL_PIXBUF,
					   NULL);
      cell = gtk_cell_renderer_text_new ();
      context->file_cell = cell;
      /* g_object_set (cell,
		    "editable", TRUE,
		    NULL); */
      g_signal_connect (cell, "edited",
			G_CALLBACK (finish_rename), context);
      g_signal_connect (cell, "editing-canceled",
			G_CALLBACK (cancel_rename), context);
      g_object_set_data (G_OBJECT (cell), "column",
			 GINT_TO_POINTER (COL_DISPLAY_NAME));
      gtk_tree_view_column_pack_start (column, cell, TRUE);
      gtk_tree_view_column_set_attributes (column, cell,
					   "text", COL_DISPLAY_NAME,
					   NULL);

      gtk_tree_view_append_column (GTK_TREE_VIEW (icon_view),
				   GTK_TREE_VIEW_COLUMN (column));
      gtk_tree_view_column_set_title (column, "Name");

      column = gtk_tree_view_column_new ();
      gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
      gtk_tree_view_column_set_sort_column_id (column, 4);
      cell = gtk_cell_renderer_text_new ();
      gtk_cell_renderer_set_alignment (cell, 1, 0.5);
      gtk_tree_view_column_pack_start (column, cell, FALSE);
      gtk_tree_view_column_set_attributes (column, cell,
					   "text", COL_DISP_SIZE,
					   NULL);
      gtk_tree_view_append_column (GTK_TREE_VIEW (icon_view),
				   GTK_TREE_VIEW_COLUMN (column));
      gtk_tree_view_column_set_title (column, "Size");

      column = gtk_tree_view_column_new ();
      gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
      gtk_tree_view_column_set_sort_column_id (column, 6);
      cell = gtk_cell_renderer_text_new ();
      gtk_tree_view_column_pack_start (column, cell, FALSE);
      gtk_tree_view_column_set_attributes (column, cell,
					   "text", COL_MTIME,
					   NULL);
      gtk_tree_view_append_column (GTK_TREE_VIEW (icon_view),
				   GTK_TREE_VIEW_COLUMN (column));
      gtk_tree_view_column_set_title (column, "Modified");

      gtk_tree_model_get_iter_first (GTK_TREE_MODEL (context->store), &iter);
      gtk_tree_selection_select_iter (GTK_TREE_SELECTION (selection), &iter);
      gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (icon_view), TRUE);

      g_signal_connect (icon_view, "row_activated",
			G_CALLBACK (tree_item_activated), context);
      {
	GtkTreeSelection *selection = gtk_tree_view_get_selection
	  (GTK_TREE_VIEW (context->icon_view));
      g_signal_connect (selection, "changed",
			G_CALLBACK (selection_changed), context);
      }
    }
  /* else error */

  selection_changed (NULL, context);
  gtk_widget_show (icon_view);
  gtk_container_add (GTK_CONTAINER (context->sw), icon_view);
  gtk_widget_grab_focus (context->icon_view);
}

static GtkWidget *
create_jfm_window (gchar *view_dir,
		   JFMViewMode view_mode,
		   gboolean view_toolbar,
		   gboolean view_location_bar,
		   gboolean full_path_title)
{
  /* Each entry takes the following form:
     { name, stock id, label, accelerator, tooltip, callback }
     Any parts not specified take on default zero values.  */
  /* Global action group entries come first.  */
  GtkActionEntry g_action_entries[] = {
    { "Paste", GTK_STOCK_PASTE, _("_Paste"), "<control>V",
      _("Paste files from clipboard"),
      G_CALLBACK (activate_action) },
    { "Undo", GTK_STOCK_UNDO, _("Undo"), "<control>Z",
      _("Undo the previous action"),
      G_CALLBACK (activate_action) },
    { "Redo", GTK_STOCK_REDO, _("Redo"), "<control>Y",
      _("Redo"),
      G_CALLBACK (activate_action) },
  };
  guint g_n_action_entries = G_N_ELEMENTS (g_action_entries);
  GtkToggleActionEntry g_toggle_entries[] = {
    { "HideDotFiles", NULL, _("Hide D_ot Files"), NULL,
      _("Show/hide dot-files"),
      G_CALLBACK (activate_action), g_hide_dotfiles },
  };
  guint g_n_toggle_entries = G_N_ELEMENTS (g_toggle_entries);
  GtkActionEntry entries[] = {
    { "FileMenu", NULL, _("_File") },
    { "EditMenu", NULL, _("_Edit") },
    { "GoMenu", NULL, _("_Go") },
    { "ViewMenu", NULL, _("_View") },
    { "HelpMenu", NULL, _("_Help") },
    { "NewWindow", GTK_STOCK_ADD, _("_New Window"), "<control>N",
      _("Create a new window"),
      G_CALLBACK (activate_action) },
    { "Open", GTK_STOCK_OPEN, _("_Open..."), "<control>O",
      _("Open an existing file"),
      G_CALLBACK (activate_action) },
    { "Save", GTK_STOCK_SAVE, _("_Save"),"<control>S",
      _("Save the current file"),
      G_CALLBACK (activate_action) },
    { "SaveAs", GTK_STOCK_SAVE_AS, _("Save _As..."), NULL,
      _("Save the current file with a new name"),
      G_CALLBACK (activate_action) },
    { "CloseWindow", GTK_STOCK_QUIT, _("_Close Window"), "<control>Q",
      _("Close the current window"),
      G_CALLBACK (activate_action) },
    { "NewFolder", GTK_STOCK_DIRECTORY, _("_New Folder"), "<control>B",
      _("Create a new folder"),
      G_CALLBACK (activate_action) },
    { "NewFile", GTK_STOCK_FILE, _("New Fi_le"), NULL,
      _("Create a new file"),
      G_CALLBACK (activate_action) },
    { "Cut", GTK_STOCK_CUT, _("Cu_t"), "<control>X",
      _("Cut the selected file(s)"),
      G_CALLBACK (activate_action) },
    /* { "Copy", GTK_STOCK_COPY, _("_Copy"), "<control>C",
      _("Copy the selected file(s)"),
      G_CALLBACK (activate_action) }, */
    { "Rename", GTK_STOCK_EDIT, _("_Rename"), "F2",
      _("Rename the selected file"),
      G_CALLBACK (activate_action) },
    { "Delete", GTK_STOCK_DELETE, _("_Delete"), "Delete",
      _("Delete the selected file(s)"),
      G_CALLBACK (activate_action) },
    { "SelectAll", NULL, _("_Select All"), "<control>A",
      _("Select all files in the window"),
      G_CALLBACK (activate_action) },
    { "InvertSelection", NULL, _("_Invert Selection"), "<control>I",
      _("Invert the selected files in the window"),
      G_CALLBACK (activate_action) },
    { "Back", GTK_STOCK_GO_BACK, _("_Back"), "<alt>Left",
      _("Go back"),
      G_CALLBACK (activate_action) },
    { "Forward", GTK_STOCK_GO_FORWARD, _("_Forward"), "<alt>Right",
      _("Go forward"),
      G_CALLBACK (activate_action) },
    { "Up", GTK_STOCK_GO_UP, _("_Up"), "<alt>Up",
      _("Go up"),
      G_CALLBACK (activate_action) },
    { "Home", GTK_STOCK_HOME, _("_Home"), "<alt>Home",
      _("Go home"),
      G_CALLBACK (activate_action) },
    { "AddressBar", NULL, _("_Address Bar"), "<control>L",
      _("Go to the address bar"),
      G_CALLBACK (activate_action) },
    { "About", GTK_STOCK_ABOUT, _("_About"), NULL,
      _("Display program name and copyright information"),
      G_CALLBACK (activate_action) },
  };
  guint n_entries = G_N_ELEMENTS (entries);
  GtkRadioActionEntry view_entries[] = {
    { "Icons", NULL, _("_Icons"), NULL,
      _("View large icons"),
      JFM_VIEW_ICONS },
    { "SmallIcons", NULL, _("_Small Icons"), NULL,
      _("View small icons"),
      JFM_VIEW_SM_ICONS },
    { "Details", NULL, _("_Details"), NULL,
      _("View detailed listing"),
      JFM_VIEW_DETAILS },
  };
  GtkToggleActionEntry toggle_entries[] = {
    { "ViewMenuBar", NULL, _("_Menu bar"), "F9",
      _("Show/hide the menu bar"),
      G_CALLBACK (activate_action), TRUE },
    { "ViewToolBar", NULL, _("_Toolbar"), NULL,
      _("Show/hide the toolbar"),
      G_CALLBACK (activate_action), view_toolbar },
    { "ViewLocationBar", NULL, _("_Address bar"), NULL,
      _("Show/hide the address bar"),
      G_CALLBACK (activate_action), view_location_bar },
    { "FullPathTitle", NULL, _("_Full path title"), NULL,
      _("Show the full path in the window title bar"),
      G_CALLBACK (activate_action), full_path_title },
  };
  guint n_toggle_entries = G_N_ELEMENTS (toggle_entries);
  guint n_view_entries = G_N_ELEMENTS (view_entries);
  GError *error = NULL;
  JFMContext *context = (JFMContext *) g_malloc (sizeof (JFMContext));
  GtkWidget *new_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  GtkWidget *sw;
  GtkListStore *store;
  GtkWidget *vbox;
  GtkWidget *tool_bar;
  GtkWidget *location_bar;

  context->view_mode = view_mode;
  /* Note: We intentionally do not allow inheriting "hide menu bar"
     because this can be confusing.  Nevertheless, we still allow
     hiding the menu bar in the interest of geek users.  (Who else
     would be using a journaling file manager?)  */
  context->view_menubar = TRUE;
  context->view_toolbar = view_location_bar;
  context->view_location_bar = view_toolbar;
  context->full_path_title = full_path_title;
  context->browshist_stack = g_array_new (FALSE, FALSE, sizeof (BrowseHist));
  context->browshist_pos = 0;
  g_array_append_val (g_undoctx.contexes, context);
  context->creating_file = FALSE;
  context->finishing_rename = FALSE;

  context->window = new_window;
  g_num_windows++;
  gtk_window_set_default_size (GTK_WINDOW (new_window), 650, 400);
  gtk_window_set_title (GTK_WINDOW (new_window), _("jfman"));

  g_signal_connect (new_window, "destroy",
		    G_CALLBACK (close_window), context);
  g_signal_connect (new_window, "delete-event",
		    G_CALLBACK (delete_window), context);

  if (!load_pixbufs (new_window, &error))
    {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (GTK_WINDOW (new_window),
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

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (new_window), vbox);
  gtk_widget_show (vbox);

  { /* Build the menu bar and tool bar.  */
    GtkWidget *menu_bar;
    GtkActionGroup *action_group;
    GtkUIManager *merge;
    GError *error;

    if (g_action_group == NULL)
      {
	g_action_group = gtk_action_group_new ("GlobalAppActions");
	gtk_action_group_add_actions (g_action_group,
				      g_action_entries, g_n_action_entries,
				      context);
	gtk_action_group_add_toggle_actions
	  (g_action_group,
	   g_toggle_entries, g_n_toggle_entries,
	   context);
	g_undo_action =
	  gtk_action_group_get_action (g_action_group, "Undo");
	g_redo_action =
	  gtk_action_group_get_action (g_action_group, "Redo");
	g_paste_action =
	  gtk_action_group_get_action (g_action_group, "Paste");

	if (g_undoctx.pos == 0)
	  gtk_action_set_sensitive (g_undo_action, FALSE);
	if (g_undoctx.pos >= g_undoctx.stack->len)
	  gtk_action_set_sensitive (g_redo_action, FALSE);
	{
	  gboolean paste_sens = g_cut_buffer.parent != NULL ? TRUE : FALSE;
	  gtk_action_set_sensitive (g_paste_action, paste_sens);
	}
      }

    action_group = gtk_action_group_new ("AppWindowActions");
    gtk_action_group_add_actions (action_group,
				  entries, n_entries,
				  context);
    gtk_action_group_add_radio_actions (action_group,
					view_entries, n_view_entries,
					context->view_mode,
					G_CALLBACK (activate_radio_action),
					context);
    gtk_action_group_add_toggle_actions (action_group,
					 toggle_entries, n_toggle_entries,
					 context);
    merge = gtk_ui_manager_new ();
    g_object_set_data_full (G_OBJECT (new_window), "ui-manager", merge,
			    g_object_unref);
    gtk_ui_manager_insert_action_group (merge, g_action_group, 0);
    gtk_ui_manager_insert_action_group (merge, action_group, 0);
    gtk_window_add_accel_group (GTK_WINDOW (new_window),
				gtk_ui_manager_get_accel_group (merge));
    if (!gtk_ui_manager_add_ui_from_string (merge, ui_info, -1, &error))
      {
	g_message (_("building menus failed: %s"), error->message);
	g_error_free (error);
      }
    menu_bar = gtk_ui_manager_get_widget (merge, "/MenuBar");
    context->menu_bar = menu_bar;
    gtk_widget_show (menu_bar);
    gtk_box_pack_start (GTK_BOX (vbox), menu_bar, FALSE, FALSE, 0);
    tool_bar = gtk_ui_manager_get_widget (merge, "/ToolBar");
    context->tool_bar = tool_bar;
    gtk_toolbar_set_style (GTK_TOOLBAR (tool_bar), GTK_TOOLBAR_ICONS);
    if (view_toolbar)
      gtk_widget_show (tool_bar);
    else
      gtk_widget_hide (tool_bar);
    gtk_box_pack_start (GTK_BOX (vbox), tool_bar, FALSE, FALSE, 0);

    context->back_action =
      gtk_action_group_get_action (action_group, "Back");
    context->forward_action =
      gtk_action_group_get_action (action_group, "Forward");
    context->up_action =
      gtk_action_group_get_action (action_group, "Up");
    context->cut_action =
      gtk_action_group_get_action (action_group, "Cut");
    context->rename_action =
      gtk_action_group_get_action (action_group, "Rename");
    context->delete_action =
      gtk_action_group_get_action (action_group, "Delete");

    gtk_action_set_sensitive (context->back_action, FALSE);
    gtk_action_set_sensitive (context->forward_action, FALSE);
    gtk_action_set_sensitive (context->delete_action, !g_disable_delete);
  }

  /* Create the location bar.  */
  location_bar = gtk_entry_new ();
  context->location_bar = location_bar;
  if (view_location_bar)
    gtk_widget_show (location_bar);
  else
    gtk_widget_hide (location_bar);
  gtk_box_pack_start (GTK_BOX (vbox), location_bar, FALSE, FALSE, 0);

  g_signal_connect (location_bar, "activate",
		    G_CALLBACK (location_bar_activate), context);

  /* Create the scroll area for the icon view.  */
  sw = gtk_scrolled_window_new (NULL, NULL);
  context->sw = sw;
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
				       GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_widget_show (sw);

  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);

  /* Create the store and fill it with the contents of '/' */
  store = create_store ();
  context->store = store;
  context->parent = NULL;
  context->icon_view = NULL;
  push_directory (context, g_strdup (view_dir));

  /* Create the icon view.  */
  set_view_mode (context);

  if (!GTK_WIDGET_VISIBLE (new_window))
    gtk_widget_show (new_window);
  else
    gtk_widget_destroy (new_window);

  return new_window;
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

  g_undoctx.stack = g_array_new (FALSE, FALSE, sizeof (UndoHist));
  g_undoctx.pos = 0;
  g_undoctx.contexes = g_array_new (FALSE, FALSE, sizeof (JFMContext_ptr));
  g_cut_buffer.oper = JFM_MOVE;
  g_cut_buffer.parent = NULL;
  g_cut_buffer.t.move.files =
    g_array_new (FALSE, FALSE, sizeof (JFMContext_ptr));
  g_cut_buffer.t.move.new_parent = NULL;
  g_drag_src = NULL;
  g_timeout_add_seconds (600, do_autosave, NULL);
  create_jfm_window ("/", JFM_VIEW_DETAILS, TRUE, TRUE, FALSE);
  /* g_signal_connect ((gpointer) main_window, "destroy",
		    G_CALLBACK (gtk_main_quit), NULL); */

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
