/* Miscellaneous support functions.

This file is part of jfman.

jfman is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your
option) any later version.

jfman is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, see
<http://www.gnu.org/licenses/gpl-3.0.html>.  */

/**
 * @file
 * Miscellaneous support function declarations.
 */

#ifndef SUPPORT_H
#define SUPPORT_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>

/*
 * Standard gettext macros.
 */
#ifdef ENABLE_NLS
#  include <libintl.h>
#  undef _
#  define _(String) dgettext (PACKAGE, String)
#  define Q_(String) g_strip_context ((String), gettext (String))
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#else
#  define textdomain(String) (String)
#  define gettext(String) (String)
#  define dgettext(Domain,Message) (Message)
#  define dcgettext(Domain,Message,Type) (Message)
#  define bindtextdomain(Domain,Directory) (Domain)
#  define _(String) (String)
#  define Q_(String) g_strip_context ((String), (String))
#  define N_(String) (String)
#endif


/*
 * Public Functions.
 */

/*
 * This function returns a widget in a component created by Glade.
 * Call it with the toplevel widget in the component (i.e. a window/dialog),
 * or alternatively any widget in the component, and the name of the widget
 * you want returned.
 */
GtkWidget *lookup_widget (GtkWidget * widget, const gchar * widget_name);


/* Use this function to set the directory containing installed pixmaps.  */
void add_pixmap_directory (const gchar * directory);


/*
 * Private Functions.
 */

/* This is used to create the pixmaps used in the interface.  */
GtkWidget *create_pixmap (GtkWidget * widget, const gchar * filename);

/* This is used to create the pixbufs used in the interface.  */
GdkPixbuf *create_pixbuf (const gchar * filename);

/* This is used to set ATK action descriptions.  */
void glade_set_atk_action_description (AtkAction * action,
				       const gchar * action_name,
				       const gchar * description);

/* These variables define various paths detected at runtime.  */
extern gchar *package_prefix;
extern gchar *package_data_dir;
extern gchar *package_locale_dir;

#endif /* not SUPPORT_H */
