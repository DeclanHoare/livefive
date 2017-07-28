/*
	Copyright 2017 Declan Hoare

	This file is part of LiveFive.
	LiveFive is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	LiveFive is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with LiveFive.  If not, see <http://www.gnu.org/licenses/>.

	livefive.c - the editor
*/

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <webkit2/webkit2.h>

#include "lfstrings.x"

/* these are not in lfstrings.x as they're not localisable */
const char file_uri[] = "file://";

char* docname;
char* docuri;
size_t docname_len;
bool modified = false, live = true, blocknext = true;
GtkWidget* main_window;
GtkWidget* edit_grid = NULL;
GtkOrientation grid_orientation;
GtkWidget* web_view = NULL;
GtkWidget* text_scroll = NULL;
GtkWidget* text_view = NULL;
GtkSourceBuffer* text_buffer;
char* text = NULL;

#define ERROR_DLG(...) { GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, __VA_ARGS__); gtk_dialog_run(GTK_DIALOG(dialog)); gtk_widget_destroy(dialog); }

void reorient_grid(GtkOrientation orientation)
{
	g_object_ref(G_OBJECT(web_view)); /* garbage collection in C... */
	g_object_ref(G_OBJECT(text_scroll));
	if (edit_grid)
	{
		switch (grid_orientation)
		{
			case GTK_ORIENTATION_HORIZONTAL:
				gtk_grid_remove_row(GTK_GRID(edit_grid), 0);
				gtk_grid_remove_row(GTK_GRID(edit_grid), 0);
				break;
			case GTK_ORIENTATION_VERTICAL:
				gtk_grid_remove_column(GTK_GRID(edit_grid), 0);
				gtk_grid_remove_column(GTK_GRID(edit_grid), 0);
				break;
		}
	}
	else
	{
		edit_grid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(edit_grid), 4);
		gtk_grid_set_column_spacing(GTK_GRID(edit_grid), 4);
	}
	switch (orientation)
	{
		case GTK_ORIENTATION_HORIZONTAL:
			gtk_grid_insert_row(GTK_GRID(edit_grid), 0);
			gtk_grid_attach(GTK_GRID(edit_grid), web_view, 0, 0, 1, 1);
			gtk_grid_insert_row(GTK_GRID(edit_grid), 1);
			gtk_grid_attach(GTK_GRID(edit_grid), text_scroll, 1, 0, 1, 1);
			break;
		case GTK_ORIENTATION_VERTICAL:
			gtk_grid_insert_column(GTK_GRID(edit_grid), 0);
			gtk_grid_attach(GTK_GRID(edit_grid), web_view, 0, 0, 1, 1);
			gtk_grid_insert_column(GTK_GRID(edit_grid), 1);
			gtk_grid_attach(GTK_GRID(edit_grid), text_scroll, 0, 1, 1, 1);
			break;
	}
	grid_orientation = orientation;
	gtk_widget_show_all(edit_grid);
	g_object_unref(G_OBJECT(text_scroll));
	g_object_unref(G_OBJECT(web_view));
}

void reorient_hbox(void)
{
	reorient_grid(GTK_ORIENTATION_HORIZONTAL);
}

void reorient_vbox(void)
{
	reorient_grid(GTK_ORIENTATION_VERTICAL);
}

void update_title(void)
{
	static char* title;
	if (title)
		free(title);
	const char* doctitle = webkit_web_view_get_title(WEBKIT_WEB_VIEW(web_view));
	size_t doctitle_len;
	if (doctitle)
		doctitle_len = strlen(doctitle);
	else
		doctitle_len = 0;
	title = malloc(docname_len + sizeof(title_suffix) + modified + (doctitle_len ? doctitle_len + 2 : 0));
	size_t i = 0;
	if (modified)
	{
		title[i] = '*';
		i++;
	}
	if (doctitle_len)
	{
		memcpy(title + i, doctitle, doctitle_len);
		i += doctitle_len;
		title[i] = '(';
		i++;
	}
	memcpy(title + i, docname, docname_len);
	i += docname_len;
	if (doctitle_len)
	{
		title[i] = ')';
		i++;
	}
	memcpy(title + i, title_suffix, sizeof(title_suffix));
	gtk_window_set_title(GTK_WINDOW(main_window), title);
}

void text_to_buffer(void)
{
	g_free(text);
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(text_buffer), &start, &end);
	text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(text_buffer), &start, &end, true);
}

void text_to_web(void)
{
	text_to_buffer();
	blocknext = false;
	webkit_web_view_load_html(WEBKIT_WEB_VIEW(web_view), text, docuri);
}

/* free the strings if they aren't constant */
void free_docname(void)
{
	if (docname != text_untitled)
		g_free(docname);
}

void generate_local_docuri(void)
{
	g_free(docuri);
	if (docname == text_untitled)
		docuri = NULL;
	else
	{
		docuri = g_malloc(docname_len + sizeof(file_uri));
		memcpy(docuri, file_uri, sizeof(file_uri) - 1);
		memcpy(docuri + sizeof(file_uri) - 1, docname, docname_len + 1);
	}
}

bool choose_name(void)
{
	GtkWidget* dialog = gtk_file_chooser_dialog_new(text_save_as, GTK_WINDOW(main_window), GTK_FILE_CHOOSER_ACTION_SAVE, text_cancel, GTK_RESPONSE_CANCEL, text_save, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), true);
	if (docname[0] == '/') /* absolute path */
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), docname);
	else
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), text_untitled);
	switch (gtk_dialog_run(GTK_DIALOG(dialog)))
	{
		case GTK_RESPONSE_CANCEL:
			gtk_widget_destroy(dialog);
			return false;
		case GTK_RESPONSE_ACCEPT:
			free_docname();
			docname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
			docname_len = strlen(docname);
			generate_local_docuri();
			gtk_widget_destroy(dialog);
			return true;
	}
}

bool save(void)
{
	if (docname[0] != '/') /* untitled or remote file */
		if (!choose_name())
			return false;
	GtkTextIter start, end;
	if (!live)
		text_to_buffer();
	FILE* fobj = fopen(docname, "w");
	fwrite(text, 1, strlen(text), fobj);
	fclose(fobj);
	modified = false;
	update_title();
	return true;
}

bool save_as(void)
{
	if (!choose_name())
		return false;
	return save();
}

bool verify_close(void)
{
	if (modified)
	{
		GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE, unsaved_changes_message, docname);
		gtk_dialog_add_buttons(GTK_DIALOG(dialog), text_yes, GTK_RESPONSE_YES, text_no, GTK_RESPONSE_NO, text_cancel, GTK_RESPONSE_CANCEL, NULL);
		gtk_window_set_title(GTK_WINDOW(dialog), unsaved_changes_title);
		gint response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		switch (response)
		{
			case GTK_RESPONSE_YES:
				return save();
			case GTK_RESPONSE_NO:
				return true;
			case GTK_RESPONSE_CANCEL:
				return false;
		}
	}
	return true;
}

bool verify_quit(void)
{
	if (verify_close())
		gtk_main_quit();
	return true;
}

void modify(void)
{
	if (!modified)
	{
		modified = true;
		update_title();
	}
	if (live)
		text_to_web();
}

void new(void)
{
	if (verify_close())
	{
		g_free(text);
		text_buffer = gtk_source_buffer_new(NULL);
		gtk_text_view_set_buffer(GTK_TEXT_VIEW(text_view), GTK_TEXT_BUFFER(text_buffer));
		gtk_source_buffer_set_language(text_buffer, gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "html"));
		g_signal_connect(G_OBJECT(text_buffer), "changed", G_CALLBACK(modify), NULL);
		g_object_unref(G_OBJECT(text_buffer));
		blocknext = false;
		webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), "about:blank");
		free_docname();
		docname = (char*) text_untitled;
		docname_len = sizeof(text_untitled) - 1;
		generate_local_docuri();
		modified = false;
		update_title();
	}
}

void toggle_live(void)
{
	live = !live;
	if (live)
		text_to_web();
}

void open_file(void)
{
	if (verify_close())
	{
		char* new_docname = NULL;
		char* data = NULL;
		FILE* fobj = NULL;
		struct stat our_stat;
		GtkWidget* dialog = gtk_file_chooser_dialog_new(text_open, GTK_WINDOW(main_window), GTK_FILE_CHOOSER_ACTION_OPEN, text_cancel, GTK_RESPONSE_CANCEL, text_open, GTK_RESPONSE_ACCEPT, NULL);
		switch (gtk_dialog_run(GTK_DIALOG(dialog)))
		{
			case GTK_RESPONSE_ACCEPT:
				new_docname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
				if (stat(new_docname, &our_stat)) /* returns 0 on success... */
				{
					ERROR_DLG(error_stat, new_docname);
					break;
				}
				
				fobj = fopen(new_docname, "r");
				if (!fobj)
				{
					ERROR_DLG(error_fopen, new_docname);
					break;
				}
				data = malloc(our_stat.st_size);
				if (!data)
				{
					ERROR_DLG(error_malloc);
					break;
				}
				fread(data, 1, our_stat.st_size, fobj);
				fclose(fobj);
				fobj = NULL;
				
				free_docname();
				docname = new_docname;
				new_docname = NULL;
				docname_len = strlen(docname);
				generate_local_docuri();
				webkit_web_view_load_html(WEBKIT_WEB_VIEW(web_view), data, docuri);
				gtk_text_buffer_set_text(GTK_TEXT_BUFFER(text_buffer), data, our_stat.st_size);
				free(data);
				data = NULL;
				modified = false;
				update_title();
				blocknext = false;
		}
		if (fobj)
			fclose(fobj);
		if (data)
			free(data);
		g_free(new_docname);
		gtk_widget_destroy(dialog);
	}
}

bool nav_policy(WebKitWebView* this_web_view, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer user_data)
{
	if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
		if (blocknext) /* will be set to false before handled navigations */
		{
			webkit_policy_decision_ignore(decision);
			return true;
		}
		else
			blocknext = true;
	return false; /* allow other actions */
}

int main(int argc, char** argv)
{
	gtk_init(&argc, &argv);
	main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(main_window), vbox);
	
	GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, false, false, 0);
	
	GtkWidget* menubar = gtk_menu_bar_new();
	
	GtkWidget* menu_file = gtk_menu_new();
	GtkWidget* menu_file_item = gtk_menu_item_new_with_label(menu_file_text);
	GtkWidget* menu_file_new = gtk_menu_item_new_with_label(menu_file_new_text);
	GtkWidget* menu_file_open = gtk_menu_item_new_with_label(menu_file_open_text);
	GtkWidget* menu_file_save = gtk_menu_item_new_with_label(menu_file_save_text);
	GtkWidget* menu_file_save_as = gtk_menu_item_new_with_label(menu_file_save_as_text);
	GtkWidget* menu_file_quit = gtk_menu_item_new_with_label(menu_file_quit_text);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_file_item), menu_file);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_file_new);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_file_open);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_file_save);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_file_save_as);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_file_quit);
	
	GtkWidget* menu_view = gtk_menu_new();
	GtkWidget* menu_view_item = gtk_menu_item_new_with_label(menu_view_text);
	GtkWidget* menu_view_split = gtk_menu_new();
	GtkWidget* menu_view_split_item = gtk_menu_item_new_with_label(menu_view_split_text);
	GtkWidget* menu_view_split_horizontal = gtk_radio_menu_item_new_with_label(NULL, menu_view_split_horizontal_text);
	GtkWidget* menu_view_split_vertical = gtk_radio_menu_item_new_with_label(gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_view_split_horizontal)), menu_view_split_vertical_text);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_view_item), menu_view);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_view), menu_view_split_item);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_view_split_item), menu_view_split);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_view_split), menu_view_split_horizontal);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_view_split), menu_view_split_vertical);
	
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menu_file_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menu_view_item);
	gtk_box_pack_start(GTK_BOX(hbox), menubar, true, true, 0);
	
	GtkWidget* live_checkbox = gtk_check_button_new_with_label(text_live);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(live_checkbox), true);
	gtk_box_pack_start(GTK_BOX(hbox), live_checkbox, false, false, 0);

	web_view = webkit_web_view_new();
	text_scroll = gtk_scrolled_window_new(NULL, NULL);
	text_view = gtk_source_view_new();
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), true);
	gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(text_view), true);
	gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(text_view), true);
	gtk_source_view_set_indent_on_tab(GTK_SOURCE_VIEW(text_view), true);
	gtk_source_view_set_smart_home_end(GTK_SOURCE_VIEW(text_view), GTK_SOURCE_SMART_HOME_END_BEFORE);
	gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(text_view), 4);
	
	gtk_container_add(GTK_CONTAINER(text_scroll), text_view);
	g_object_set(web_view, "expand", true, NULL);
	g_object_set(text_scroll, "expand", true, NULL);
	
	new();
	
	reorient_grid(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(vbox), edit_grid, true, true, 0);
	
	gtk_widget_show_all(main_window);
	
	g_signal_connect(G_OBJECT(main_window), "delete_event", G_CALLBACK(verify_quit), NULL);
	g_signal_connect(G_OBJECT(main_window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
	
	g_signal_connect(G_OBJECT(web_view), "notify::title", G_CALLBACK(update_title), NULL);
	g_signal_connect(G_OBJECT(web_view), "decide-policy", G_CALLBACK(nav_policy), NULL);
	
	g_signal_connect(G_OBJECT(menu_file_new), "activate", G_CALLBACK(new), NULL);
	g_signal_connect(G_OBJECT(menu_file_open), "activate", G_CALLBACK(open_file), NULL);
	g_signal_connect(G_OBJECT(menu_file_save), "activate", G_CALLBACK(save), NULL);
	g_signal_connect(G_OBJECT(menu_file_save_as), "activate", G_CALLBACK(save_as), NULL);
	g_signal_connect(G_OBJECT(menu_file_quit), "activate", G_CALLBACK(verify_quit), NULL);
	g_signal_connect(G_OBJECT(menu_view_split_horizontal), "activate", G_CALLBACK(reorient_hbox), NULL);
	g_signal_connect(G_OBJECT(menu_view_split_vertical), "activate", G_CALLBACK(reorient_vbox), NULL);
	
	g_signal_connect(G_OBJECT(live_checkbox), "toggled", G_CALLBACK(toggle_live), NULL);
	
	gtk_main();
	return 0;
}

