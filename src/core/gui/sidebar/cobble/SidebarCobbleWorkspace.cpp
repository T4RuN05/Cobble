#include "SidebarCobbleWorkspace.h"
#include "control/Control.h"
#include "cobble/CobbleSyncEngine.h"
#include "control/settings/Settings.h"
#include "util/PathUtil.h"
#include <gtk/gtk.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace xoj {
namespace cobble {

enum {
    COL_ICON_NAME = 0,
    COL_DISPLAY_NAME,
    COL_FILE_PATH, // Hidden absolute path
    COL_SYNC_ICON,
    COL_IS_SYNCING,
    COL_TOOLTIP,
    COL_SPINNER_PULSE,
    NUM_COLS
};

SidebarCobbleWorkspace::SidebarCobbleWorkspace(Control* control) : AbstractSidebarPage(control) {
    m_widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    
    // Header Box
    GtkWidget* headerBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(m_widget), headerBox, FALSE, FALSE, 0);

    GtkWidget* btnSelectFolder = gtk_button_new_with_label("Select Workspace Folder");
    gtk_box_pack_start(GTK_BOX(headerBox), btnSelectFolder, FALSE, FALSE, 0);

    m_pathLabel = gtk_label_new("No workspace selected.");
    gtk_label_set_line_wrap(GTK_LABEL(m_pathLabel), TRUE);
    gtk_label_set_xalign(GTK_LABEL(m_pathLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(headerBox), m_pathLabel, FALSE, FALSE, 0);

    // Separator
    GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(m_widget), separator, FALSE, FALSE, 0);

    // Scrollable Tree View
    GtkWidget* scrolledWindow = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_vexpand(scrolledWindow, TRUE);
    gtk_box_pack_start(GTK_BOX(m_widget), scrolledWindow, TRUE, TRUE, 0);

    m_treeStore = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT);
    m_treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(m_treeStore));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(m_treeView), FALSE);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(m_treeView), COL_TOOLTIP);
    
    // Create column for icon + text
    GtkTreeViewColumn* column = gtk_tree_view_column_new();
    
    GtkCellRenderer* iconRenderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, iconRenderer, FALSE);
    gtk_tree_view_column_add_attribute(column, iconRenderer, "icon-name", COL_ICON_NAME);

    GtkCellRenderer* textRenderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, textRenderer, TRUE);
    gtk_tree_view_column_add_attribute(column, textRenderer, "text", COL_DISPLAY_NAME);

    // Sync icon (cloud / error)
    GtkCellRenderer* syncIconRenderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, syncIconRenderer, FALSE);
    gtk_tree_view_column_add_attribute(column, syncIconRenderer, "pixbuf", COL_SYNC_ICON);
    
    // Sync spinner
    GtkCellRenderer* spinnerRenderer = gtk_cell_renderer_spinner_new();
    gtk_tree_view_column_pack_start(column, spinnerRenderer, FALSE);
    gtk_tree_view_column_add_attribute(column, spinnerRenderer, "active", COL_IS_SYNCING);
    gtk_tree_view_column_add_attribute(column, spinnerRenderer, "pulse", COL_SPINNER_PULSE);
    gtk_tree_view_column_add_attribute(column, spinnerRenderer, "visible", COL_IS_SYNCING);

    gtk_tree_view_append_column(GTK_TREE_VIEW(m_treeView), column);
    gtk_container_add(GTK_CONTAINER(scrolledWindow), m_treeView);

    // Signals
    g_signal_connect(btnSelectFolder, "clicked", G_CALLBACK(+[](GtkWidget* widget, gpointer data) {
        auto* self = static_cast<SidebarCobbleWorkspace*>(data);
        
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Select Cobble Workspace Folder",
            self->getControl()->getGtkWindow(),
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Open", GTK_RESPONSE_ACCEPT,
            nullptr
        );
        
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            CobbleSyncEngine::getInstance().setRootPath(folder);
            
            std::string labelText = "Workspace: \n" + std::string(folder);
            gtk_label_set_text(GTK_LABEL(self->m_pathLabel), labelText.c_str());
            
            self->getControl()->getSettings()->setCobbleWorkspacePath(fs::path(folder));

            g_free(folder);
            self->populateTree();
        }
        
        gtk_widget_destroy(dialog);
    }), this);

    g_signal_connect(m_treeView, "row-activated", G_CALLBACK(+[](GtkTreeView* treeView, GtkTreePath* path, GtkTreeViewColumn* col, gpointer data) {
        auto* self = static_cast<SidebarCobbleWorkspace*>(data);
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(self->m_treeStore), &iter, path)) {
            gchar* filePath = nullptr;
            gtk_tree_model_get(GTK_TREE_MODEL(self->m_treeStore), &iter, COL_FILE_PATH, &filePath, -1);
            
            if (filePath && g_utf8_strlen(filePath, -1) > 0) {
                // It's a file, open it!
                self->getControl()->openFile(filePath);
            } else {
                // It's a folder, toggle expansion
                if (gtk_tree_view_row_expanded(treeView, path)) {
                    gtk_tree_view_collapse_row(treeView, path);
                } else {
                    gtk_tree_view_expand_row(treeView, path, FALSE);
                }
            }
            g_free(filePath);
        }
    }), this);

    // Hardcode SVG strings to avoid any GTK path/cache/encoding issues on Windows
    const char* syncedSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-cloud-check"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"/><path d="m9 16 2 2 4-4"/></svg>)";
    const char* errorSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#e85d75" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="lucide lucide-cloud-off"><path d="m2 2 20 20"/><path d="M5.782 5.782A7 7 0 0 0 9 19h8.5a4.5 4.5 0 0 0 1.307-.193"/><path d="M21.532 16.5A4.5 4.5 0 0 0 17.5 10h-1.79A7.008 7.008 0 0 0 10 5.224"/></svg>)";

    GInputStream* syncStream = g_memory_input_stream_new_from_data(syncedSvg, -1, nullptr);
    m_iconSynced = gdk_pixbuf_new_from_stream_at_scale(syncStream, 16, 16, TRUE, nullptr, nullptr);
    g_object_unref(syncStream);

    GInputStream* errorStream = g_memory_input_stream_new_from_data(errorSvg, -1, nullptr);
    m_iconError = gdk_pixbuf_new_from_stream_at_scale(errorStream, 16, 16, TRUE, nullptr, nullptr);
    g_object_unref(errorStream);

    m_pulseTimerId = g_timeout_add(100, [](gpointer data) -> gboolean {
        auto* self = static_cast<SidebarCobbleWorkspace*>(data);
        if (!self->m_treeStore) return G_SOURCE_CONTINUE;

        gtk_tree_model_foreach(GTK_TREE_MODEL(self->m_treeStore), [](GtkTreeModel* model, GtkTreePath*, GtkTreeIter* iter, gpointer) -> gboolean {
            gboolean isSyncing = FALSE;
            guint pulse = 0;
            gtk_tree_model_get(model, iter, COL_IS_SYNCING, &isSyncing, COL_SPINNER_PULSE, &pulse, -1);
            if (isSyncing) {
                gtk_tree_store_set(GTK_TREE_STORE(model), iter, COL_SPINNER_PULSE, pulse + 1, -1);
            }
            return FALSE;
        }, nullptr);

        return G_SOURCE_CONTINUE;
    }, this);

    gtk_widget_show_all(m_widget);

    // Initial load from settings
    std::string savedPath = getControl()->getSettings()->getCobbleWorkspacePath().string();
    if (!savedPath.empty() && fs::exists(savedPath)) {
        CobbleSyncEngine::getInstance().setRootPath(savedPath);
        std::string labelText = "Workspace: \n" + savedPath;
        gtk_label_set_text(GTK_LABEL(m_pathLabel), labelText.c_str());
    }

    CobbleSyncEngine::getInstance().setListener([this](const std::string& path) {
        // Must update UI on GTK main thread
        struct UpdateData {
            SidebarCobbleWorkspace* self;
            std::string path;
        };
        auto data = new UpdateData{this, path};
        g_idle_add(+[](gpointer d) -> gboolean {
            auto* ud = static_cast<UpdateData*>(d);
            if (ud->self) {
                ud->self->updateFileState(ud->path);
            }
            delete ud;
            return G_SOURCE_REMOVE;
        }, data);
    });
}

SidebarCobbleWorkspace::~SidebarCobbleWorkspace() {
    if (m_pulseTimerId) {
        g_source_remove(m_pulseTimerId);
        m_pulseTimerId = 0;
    }
    if (m_iconSynced) g_object_unref(m_iconSynced);
    if (m_iconError) g_object_unref(m_iconError);
    if (m_treeStore) {
        g_object_unref(m_treeStore);
    }
}

void SidebarCobbleWorkspace::enableSidebar() {
    populateTree();
}

void SidebarCobbleWorkspace::disableSidebar() {}
void SidebarCobbleWorkspace::layout() {}

std::string SidebarCobbleWorkspace::getName() { return "Cobble Workspace"; }
std::string SidebarCobbleWorkspace::getIconName() { return "folder"; }
bool SidebarCobbleWorkspace::hasData() { return true; }
GtkWidget* SidebarCobbleWorkspace::getWidget() { return m_widget; }

void SidebarCobbleWorkspace::populateTree() {
    gtk_tree_store_clear(m_treeStore);

    std::string rootPath = CobbleSyncEngine::getInstance().getRootPath();
    if (rootPath.empty()) {
        return;
    }

    try {
        if (fs::exists(rootPath) && fs::is_directory(rootPath)) {
            populateNode(rootPath, nullptr);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << '\n';
    }
}

void SidebarCobbleWorkspace::populateNode(const std::string& currentPath, GtkTreeIter* parentIter) {
    // Collect entries first so we can sort them (folders first, then files)
    std::vector<fs::directory_entry> directories;
    std::vector<fs::directory_entry> files;

    for (const auto& entry : fs::directory_iterator(currentPath)) {
        if (entry.is_directory()) {
            directories.push_back(entry);
        } else if (entry.is_regular_file() && entry.path().extension() == ".xopp") {
            files.push_back(entry);
        }
    }

    // Process directories
    for (const auto& dir : directories) {
        GtkTreeIter iter;
        gtk_tree_store_append(m_treeStore, &iter, parentIter);
        
        gtk_tree_store_set(m_treeStore, &iter,
            COL_ICON_NAME, "folder",
            COL_DISPLAY_NAME, dir.path().filename().string().c_str(),
            COL_FILE_PATH, "", // Empty path for folders
            COL_SYNC_ICON, nullptr,
            COL_IS_SYNCING, FALSE,
            COL_TOOLTIP, nullptr,
            COL_SPINNER_PULSE, (guint)0,
            -1);
            
        // Recursively populate this directory
        populateNode(dir.path().string(), &iter);
    }

    // Process files
    for (const auto& file : files) {
        GtkTreeIter iter;
        gtk_tree_store_append(m_treeStore, &iter, parentIter);

        SyncState state = CobbleSyncEngine::getInstance().getSyncState(file.path().string());
        GdkPixbuf* syncPixbuf = nullptr;
        gboolean isSyncing = FALSE;
        const char* tooltip = nullptr;
        
        if (state == SyncState::Synced) {
            syncPixbuf = m_iconSynced;
            tooltip = "Up to date with Cobble Cloud";
        } else if (state == SyncState::Failed) {
            syncPixbuf = m_iconError;
            tooltip = "Sync failed";
        } else if (state == SyncState::Unsynced) {
            syncPixbuf = m_iconError; // Reuse error icon as "needs sync" icon (cloud with cross)
            tooltip = "File edited, needs to be synced to cloud";
        } else if (state == SyncState::Syncing) {
            isSyncing = TRUE;
            tooltip = "Syncing to cloud...";
        }

        gtk_tree_store_set(m_treeStore, &iter,
            COL_ICON_NAME, "text-x-generic",
            COL_DISPLAY_NAME, file.path().filename().string().c_str(),
            COL_FILE_PATH, file.path().string().c_str(),
            COL_SYNC_ICON, syncPixbuf,
            COL_IS_SYNCING, isSyncing,
            COL_TOOLTIP, tooltip,
            COL_SPINNER_PULSE, (guint)0,
            -1);
    }
}

void SidebarCobbleWorkspace::updateFileState(const std::string& filePath) {
    if (!m_treeStore) return;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(m_treeStore), &iter)) {
        updateFileStateRecursive(&iter, filePath);
    }
}

void SidebarCobbleWorkspace::updateFileStateRecursive(GtkTreeIter* iter, const std::string& filePath) {
    do {
        gchar* pathInTree = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(m_treeStore), iter, COL_FILE_PATH, &pathInTree, -1);
        if (pathInTree && filePath == pathInTree) {
            SyncState state = CobbleSyncEngine::getInstance().getSyncState(filePath);
            GdkPixbuf* syncPixbuf = nullptr;
            gboolean isSyncing = FALSE;
            const char* tooltip = nullptr;
            
            if (state == SyncState::Synced) {
                syncPixbuf = m_iconSynced;
                tooltip = "Up to date with Cobble Cloud";
            } else if (state == SyncState::Failed) {
                syncPixbuf = m_iconError;
                tooltip = "Sync failed";
            } else if (state == SyncState::Unsynced) {
                syncPixbuf = m_iconError; 
                tooltip = "File edited, needs to be synced to cloud";
            } else if (state == SyncState::Syncing) {
                isSyncing = TRUE;
                tooltip = "Syncing to cloud...";
            }
            
            gtk_tree_store_set(m_treeStore, iter,
                COL_SYNC_ICON, syncPixbuf,
                COL_IS_SYNCING, isSyncing,
                COL_TOOLTIP, tooltip,
                -1);
            g_free(pathInTree);
            return; // found it
        }
        g_free(pathInTree);

        GtkTreeIter child;
        if (gtk_tree_model_iter_children(GTK_TREE_MODEL(m_treeStore), &child, iter)) {
            updateFileStateRecursive(&child, filePath);
        }
    } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(m_treeStore), iter));
}

} // namespace cobble
} // namespace xoj
