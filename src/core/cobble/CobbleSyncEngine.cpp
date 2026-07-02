#include "CobbleSyncEngine.h"
#include "vendor/json.hpp"
#include "control/Control.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <thread>
#include <future>
#include <iostream>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace xoj {
namespace cobble {

const std::string SUPABASE_URL = "https://rcztclkkcpxsosptdgwe.supabase.co";
const std::string ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InJjenRjbGtrY3B4c29zcHRkZ3dlIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODI5MjUxMzYsImV4cCI6MjA5ODUwMTEzNn0.HFdE91Yrc6__cnT3sR2mePPJTYPj6wVSfObUNXJ_gQM";

void CobbleSyncEngine::setRootPath(const std::string& path) {
    m_rootPath = path;
    loadSyncStateDb();
}

std::string CobbleSyncEngine::getRootPath() const {
    return m_rootPath;
}

void CobbleSyncEngine::loadSyncStateDb() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_syncTimestamps.clear();
    if (m_rootPath.empty()) return;
    
    fs::path dbPath = fs::path(m_rootPath) / ".cobble_sync.json";
    if (fs::exists(dbPath)) {
        try {
            std::ifstream i(dbPath);
            json j;
            i >> j;
            if (j.contains("files")) {
                for (auto& el : j["files"].items()) {
                    m_syncTimestamps[el.key()] = el.value().get<long long>();
                }
            }
        } catch (...) {
            std::cerr << "Failed to load cobble_sync.json\n";
        }
    }
}

void CobbleSyncEngine::saveSyncStateDb() {
    if (m_rootPath.empty()) return;
    fs::path dbPath = fs::path(m_rootPath) / ".cobble_sync.json";
    
    try {
        json j;
        j["files"] = json::object();
        for (const auto& pair : m_syncTimestamps) {
            j["files"][pair.first] = pair.second;
        }
        
        std::ofstream o(dbPath);
        o << j.dump(4);
    } catch (...) {
        std::cerr << "Failed to save cobble_sync.json\n";
    }
}

SyncState CobbleSyncEngine::getSyncState(const std::string& absoluteFilePath) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    
    // 1. Ephemeral memory state (Syncing / Failed)
    auto it = m_fileStates.find(absoluteFilePath);
    if (it != m_fileStates.end() && (it->second == SyncState::Syncing || it->second == SyncState::Failed)) {
        return it->second;
    }

    // 2. Persistent timestamp check
    auto tsIt = m_syncTimestamps.find(absoluteFilePath);
    if (tsIt != m_syncTimestamps.end()) {
        try {
            auto currentWriteTime = fs::last_write_time(absoluteFilePath).time_since_epoch().count();
            auto storedTime = tsIt->second;
            // If the file hasn't been modified since the last successful sync
            if (currentWriteTime <= storedTime) {
                return SyncState::Synced;
            }
        } catch (...) {}
    }
    
    return SyncState::Unsynced;
}

void CobbleSyncEngine::setSyncState(const std::string& absoluteFilePath, SyncState state) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_fileStates[absoluteFilePath] = state;
    }
    if (m_listener) {
        m_listener(absoluteFilePath);
    }
}

void CobbleSyncEngine::setListener(StateChangedCallback callback) {
    m_listener = callback;
}

// -------------------------------------------------------------------------
// GTK UI Callbacks
// -------------------------------------------------------------------------
struct SyncUIState {
    GtkWidget* spinnerDlg = nullptr;
    GtkWindow* parent = nullptr;
    bool success = false;
    std::string errorMessage = "";
    std::promise<GtkWidget*> dlgPromise;
};

static gboolean show_spinner_cb(gpointer data) {
    auto* state = static_cast<SyncUIState*>(data);

    GtkWidget* dlg = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dlg), "Cobble Sync");
    if (state->parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dlg), state->parent);
    }
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_deletable(GTK_WINDOW(dlg), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 260, 120);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 22);

    GtkWidget* spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_size_request(spinner, 40, 40);
    gtk_box_pack_start(GTK_BOX(vbox), spinner, FALSE, FALSE, 0);

    GtkWidget* lbl = gtk_label_new("Syncing to Cobble Cloud...");
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(content), vbox);
    gtk_widget_show_all(dlg);

    state->dlgPromise.set_value(dlg);
    return G_SOURCE_REMOVE;
}

static gboolean show_result_cb(gpointer data) {
    auto* state = static_cast<SyncUIState*>(data);
    
    if (state->spinnerDlg) {
        gtk_widget_destroy(state->spinnerDlg);
    }

    std::string msg = state->success 
        ? "\xE2\x9C\x93  Synced to Cobble Cloud!" 
        : "\xE2\x9A\xA0  Cloud sync failed:\n" + state->errorMessage;

    GtkWidget* resultDlg = gtk_message_dialog_new(
        state->parent,
        GTK_DIALOG_MODAL,
        state->success ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK,
        "%s",
        msg.c_str()
    );
    gtk_window_set_title(GTK_WINDOW(resultDlg), "Cobble Sync");
    g_signal_connect_swapped(resultDlg, "response", G_CALLBACK(gtk_widget_destroy), resultDlg);
    gtk_widget_show_all(resultDlg);

    delete state;
    return G_SOURCE_REMOVE;
}

// -------------------------------------------------------------------------
// Helper for shell-injection-free subprocess execution
// -------------------------------------------------------------------------
static bool runCurlCommand(const std::vector<std::string>& args, std::string& outBody, std::string& errBody) {
    gchar** argv = new gchar*[args.size() + 1];
    for (size_t i = 0; i < args.size(); ++i) {
        argv[i] = g_strdup(args[i].c_str());
    }
    argv[args.size()] = nullptr;

    gchar* std_out = nullptr;
    gchar* std_err = nullptr;
    gint exit_status = 0;

    gboolean success = g_spawn_sync(
        nullptr, 
        argv, 
        nullptr, 
        G_SPAWN_SEARCH_PATH, 
        nullptr, 
        nullptr, 
        &std_out, 
        &std_err, 
        &exit_status, 
        nullptr
    );

    if (std_out) {
        outBody = std_out;
        g_free(std_out);
    }
    if (std_err) {
        errBody = std_err;
        g_free(std_err);
    }

    for (size_t i = 0; i < args.size(); ++i) {
        g_free(argv[i]);
    }
    delete[] argv;

    return success && (exit_status == 0);
}

// -------------------------------------------------------------------------
// Upload Logic
// -------------------------------------------------------------------------
void CobbleSyncEngine::uploadFileAsync(const std::string& absoluteFilePath, Control* control) {
    setSyncState(absoluteFilePath, SyncState::Syncing);

    // Generate relative path based on workspace root
    std::string relativePath = absoluteFilePath;
    
    // Windows backslash replacement
    for (char& c : relativePath) {
        if (c == '\\') c = '/';
    }

    std::string normalizedRoot = m_rootPath;
    for (char& c : normalizedRoot) {
        if (c == '\\') c = '/';
    }

    if (!normalizedRoot.empty() && relativePath.find(normalizedRoot) == 0) {
        relativePath = relativePath.substr(normalizedRoot.length());
        if (!relativePath.empty() && relativePath[0] == '/') {
            relativePath = relativePath.substr(1);
        }
    } else {
        // Fallback: just use filename if outside root or root not set
        size_t lastSlash = relativePath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            relativePath = relativePath.substr(lastSlash + 1);
        }
    }

    GtkWindow* parentWin = control ? control->getGtkWindow() : nullptr;
    auto* uiState = new SyncUIState();
    uiState->parent = parentWin;
    auto spinnerFuture = uiState->dlgPromise.get_future();
    g_idle_add(show_spinner_cb, uiState);
    
    // Background thread so we don't freeze the Xournal++ main GTK loop
    std::thread([uiState, absoluteFilePath, relativePath, spinnerFuture = std::move(spinnerFuture)]() mutable {
        uiState->spinnerDlg = spinnerFuture.get(); // block until dialog exists

        std::string authHeader = "Authorization: Bearer " + ANON_KEY;
        std::string apiHeader = "apikey: " + ANON_KEY;

        // 1. Upload Object (Upsert)
        std::string uploadUrl = SUPABASE_URL + "/storage/v1/object/cobble_docs/" + relativePath;
        std::string fileDataArg = "@" + absoluteFilePath;
        
        std::vector<std::string> uploadArgs = {
            "curl", "-s", "--fail", "-X", "POST", uploadUrl,
            "-H", authHeader,
            "-H", apiHeader,
            "-H", "Content-Type: application/octet-stream",
            "-H", "x-upsert: true",
            "--data-binary", fileDataArg
        };

        std::string outBody, errBody;
        bool uploadSuccess = runCurlCommand(uploadArgs, outBody, errBody);

        if (!uploadSuccess) {
            uiState->success = false;
            uiState->errorMessage = "Storage Upload Failed.";
            g_idle_add(show_result_cb, uiState);
            CobbleSyncEngine::getInstance().setSyncState(absoluteFilePath, SyncState::Failed);
            return;
        }

        // 2. Update Database Metadata
        std::string metaUrl = SUPABASE_URL + "/rest/v1/cobble_metadata?on_conflict=filename";
        json payload = {
            {"filename", relativePath},
            {"last_updated", "now()"}
        };
        std::string jsonStr = payload.dump();

        std::vector<std::string> metaArgs = {
            "curl", "-s", "--fail", "-X", "POST", metaUrl,
            "-H", authHeader,
            "-H", apiHeader,
            "-H", "Content-Type: application/json",
            "-H", "Prefer: resolution=merge-duplicates",
            "-d", jsonStr
        };

        bool metaSuccess = runCurlCommand(metaArgs, outBody, errBody);

        if (!metaSuccess) {
            uiState->success = false;
            uiState->errorMessage = "Metadata Sync Failed.";
            CobbleSyncEngine::getInstance().setSyncState(absoluteFilePath, SyncState::Failed);
        } else {
            uiState->success = true;
            // Record the timestamp of the synced file
            try {
                auto currentWriteTime = fs::last_write_time(absoluteFilePath).time_since_epoch().count();
                {
                    std::lock_guard<std::mutex> lock(CobbleSyncEngine::getInstance().m_stateMutex);
                    CobbleSyncEngine::getInstance().m_syncTimestamps[absoluteFilePath] = currentWriteTime;
                    CobbleSyncEngine::getInstance().saveSyncStateDb();
                }
            } catch (...) {}
            
            CobbleSyncEngine::getInstance().setSyncState(absoluteFilePath, SyncState::Synced);
        }

        g_idle_add(show_result_cb, uiState);

    }).detach();
}

} // namespace cobble
} // namespace xoj
