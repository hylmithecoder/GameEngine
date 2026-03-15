#include "../../../include/ui/utils.hpp"
#include <gtk/gtk.h>
#include <vector>
#include <string>

using namespace UI;
using namespace std;

// Helper to process GTK events to ensure UI updates and clean destruction
static void pump_gtk_events() {
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

// Helper to ensure GTK is initialized
static bool ensure_gtk_init() {
    return gtk_init_check(NULL, NULL);
}

// Helper to add filters to a chooser
static void add_filters(GtkFileChooser* chooser, nfdfilteritem_t* listItem, int count) {
    if (!listItem || count <= 0) return;

    for (int i = 0; i < count; ++i) {
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, listItem[i].name);
        
        string spec = listItem[i].spec;
        size_t start = 0;
        size_t end = spec.find(',');
        while (end != string::npos) {
            string ext = spec.substr(start, end - start);
            // Remove leading/trailing spaces from extension
            ext.erase(0, ext.find_first_not_of(" "));
            ext.erase(ext.find_last_not_of(" ") + 1);
            if (!ext.empty()) {
                string pattern = "*." + ext;
                gtk_file_filter_add_pattern(filter, pattern.c_str());
            }
            start = end + 1;
            end = spec.find(',', start);
        }
        string ext = spec.substr(start);
        ext.erase(0, ext.find_first_not_of(" "));
        ext.erase(ext.find_last_not_of(" ") + 1);
        if (!ext.empty()) {
            string pattern = "*." + ext;
            gtk_file_filter_add_pattern(filter, pattern.c_str());
        }
        
        gtk_file_chooser_add_filter(chooser, filter);
    }
}

string Utils::OpenFile(nfdfilteritem_t* listItem, int count) {
    if (!ensure_gtk_init()) return "Error: GTK init failed";

    GtkWidget* dialog = gtk_file_chooser_dialog_new("Open File",
                                                  NULL,
                                                  GTK_FILE_CHOOSER_ACTION_OPEN,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Open", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    add_filters(GTK_FILE_CHOOSER(dialog), listItem, count);

    string result = "";
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        result = filename;
        g_free(filename);
    } else {
        result = "User pressed cancel.";
    }

    gtk_widget_destroy(dialog);
    pump_gtk_events();
    return result;
}

string Utils::SaveFile(nfdfilteritem_t* listItem, int count) {
    if (!ensure_gtk_init()) return "Error: GTK init failed";

    GtkWidget* dialog = gtk_file_chooser_dialog_new("Save File",
                                                  NULL,
                                                  GTK_FILE_CHOOSER_ACTION_SAVE,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Save", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    add_filters(GTK_FILE_CHOOSER(dialog), listItem, count);

    string result = "";
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        result = filename;
        g_free(filename);
    } else {
        result = "User pressed cancel.";
    }

    gtk_widget_destroy(dialog);
    pump_gtk_events();
    return result;
}

string Utils::OpenFolder() {
    if (!ensure_gtk_init()) return "Error: GTK init failed";

    GtkWidget* dialog = gtk_file_chooser_dialog_new("Select Folder",
                                                  NULL,
                                                  GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Open", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    string result = "";
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        result = filename;
        g_free(filename);
    } else {
        result = "User pressed cancel.";
    }

    gtk_widget_destroy(dialog);
    pump_gtk_events();
    return result;
}

vector<string> Utils::OpenFiles(nfdfilteritem_t* listItem, int count) {
    vector<string> results;
    if (!ensure_gtk_init()) {
        results.push_back("Error: GTK init failed");
        return results;
    }

    GtkWidget* dialog = gtk_file_chooser_dialog_new("Open Files",
                                                  NULL,
                                                  GTK_FILE_CHOOSER_ACTION_OPEN,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Open", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
    add_filters(GTK_FILE_CHOOSER(dialog), listItem, count);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        for (GSList* iter = filenames; iter != NULL; iter = iter->next) {
            results.push_back((char*)iter->data);
            g_free(iter->data);
        }
        g_slist_free(filenames);
    } else {
        results.push_back("User pressed cancel.");
    }

    gtk_widget_destroy(dialog);
    pump_gtk_events();
    return results;
}

vector<string> Utils::OpenFolders() {
    vector<string> results;
    if (!ensure_gtk_init()) {
        results.push_back("Error: GTK init failed");
        return results;
    }

    GtkWidget* dialog = gtk_file_chooser_dialog_new("Select Folders",
                                                  NULL,
                                                  GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Open", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        for (GSList* iter = filenames; iter != NULL; iter = iter->next) {
            results.push_back((char*)iter->data);
            g_free(iter->data);
        }
        g_slist_free(filenames);
    } else {
        results.push_back("User pressed cancel.");
    }

    gtk_widget_destroy(dialog);
    pump_gtk_events();
    return results;
}
