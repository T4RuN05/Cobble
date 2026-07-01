#include "SaveJob.h"

#include <memory>  // for __shared_ptr_access
#include <cstdlib>
#include <future>
#include <string>

#include <cairo.h>  // for cairo_create, cairo_destroy
#include <glib.h>   // for g_warning, g_error

#include "control/Control.h"              // for Control
#include "control/jobs/BlockingJob.h"     // for BlockingJob
#include "control/xojfile/SaveHandler.h"  // for SaveHandler
#include "model/Document.h"               // for Document
#include "model/PageRef.h"                // for PageRef
#include "model/PageType.h"               // for PageType
#include "model/XojPage.h"                // for XojPage
#include "pdf/base/XojPdfPage.h"          // for XojPdfPageSPtr, XojPdfPage
#include "util/PathUtil.h"                // for clearExtensions, safeRename...
#include "util/XojMsgBox.h"               // for XojMsgBox
#include "util/i18n.h"                    // for FS, _, _F
#include "view/DocumentView.h"            // for DocumentView

#include "filesystem.h"  // for path, filesystem_error, remove

// ============================================================
// Cobble Cloud Sync — GTK modal helpers
// All GTK operations MUST run on the GTK main thread.
// We use g_idle_add() to schedule them from the BlockingJob
// worker thread, and std::promise/future to enforce ordering.
// ============================================================
namespace {

struct CobbleSpinnerData {
    GtkWindow* parent;
    std::promise<GtkWidget*> dlgPromise;
};

struct CobbleResultData {
    GtkWidget* spinnerDlg;
    GtkWindow* parent;
    bool success;
};

// Called on GTK main thread: builds and shows the spinner dialog.
static gboolean cobble_show_spinner(gpointer data) {
    auto* sd = static_cast<CobbleSpinnerData*>(data);

    GtkWidget* dlg = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dlg), "Cobble Sync");
    gtk_window_set_transient_for(GTK_WINDOW(dlg), sd->parent);
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

    GtkWidget* lbl = gtk_label_new("Syncing to Cobble Cloud…");
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(content), vbox);
    gtk_widget_show_all(dlg);

    // Signal the worker thread that the dialog is ready
    sd->dlgPromise.set_value(dlg);
    delete sd;
    return FALSE;
}

// Called on GTK main thread: destroys spinner, shows success/failure.
static gboolean cobble_show_result(gpointer data) {
    auto* cd = static_cast<CobbleResultData*>(data);
    gtk_widget_destroy(cd->spinnerDlg);

    GtkWidget* resultDlg = gtk_message_dialog_new(
        cd->parent,
        GTK_DIALOG_MODAL,
        cd->success ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK,
        "%s",
        cd->success ? "\xE2\x9C\x93  Synced to Cobble Cloud!"
                    : "\xE2\x9A\xA0  Cloud sync failed. File saved locally."
    );
    gtk_window_set_title(GTK_WINDOW(resultDlg), "Cobble Sync");
    g_signal_connect_swapped(resultDlg, "response", G_CALLBACK(gtk_widget_destroy), resultDlg);
    gtk_widget_show_all(resultDlg);

    delete cd;
    return FALSE;
}

} // namespace


SaveJob::SaveJob(Control* control, std::function<void(bool)> callback):
        BlockingJob(control, _("Save")), callback(std::move(callback)) {}

SaveJob::~SaveJob() = default;

void SaveJob::run() {
    save();

    if (this->control->getWindow()) {
        callAfterRun();
    }
}

void SaveJob::afterRun() {
    if (!this->lastError.empty()) {
        XojMsgBox::showErrorToUser(control->getGtkWindow(), this->lastError);
        callback(false);
    } else {
        this->control->resetSavedStatus();
        callback(true);
    }
}

void SaveJob::updatePreview(Control* control) {
    const int previewSize = 128;

    Document* doc = control->getDocument();
    xoj::util::CairoSurfaceSPtr crBuffer;

    doc->lock_shared();
    if (doc->getPageCount() > 0) {
        PageRef page = doc->getPage(0);

        double width = page->getWidth();
        double height = page->getHeight();

        double zoom = 1;

        if (width < height) {
            zoom = previewSize / height;
        } else {
            zoom = previewSize / width;
        }
        width *= zoom;
        height *= zoom;

        crBuffer.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ceil_cast<int>(width), ceil_cast<int>(height)),
                       xoj::util::adopt);

        cairo_t* cr = cairo_create(crBuffer.get());
        cairo_scale(cr, zoom, zoom);

        xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;

        // We don't have access to a PdfCache on which DocumentView relies for PDF backgrounds.
        // We thus print the PDF background by hand.
        if (page->getBackgroundType().isPdfPage()) {
            auto pgNo = page->getPdfPageNr();
            XojPdfPageSPtr popplerPage = doc->getPdfPage(pgNo);
            if (popplerPage) {
                popplerPage->render(cr);
            }
            flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;  // Already printed (if any)
        } else {
            flags.forceBackgroundColor = xoj::view::FORCE_AT_LEAST_BACKGROUND_COLOR;
        }

        DocumentView view;
        view.drawPage(page, cr, true /* don't render erasable */, flags);
        cairo_destroy(cr);
    }
    doc->unlock_shared();

    doc->lock();
    doc->setPreview(std::move(crBuffer));
    doc->unlock();
}

auto SaveJob::save() -> bool {
    updatePreview(control);
    Document* doc = this->control->getDocument();
    SaveHandler h;

    doc->lock_shared();
    fs::path target = doc->getFilepath();
    Util::safeReplaceExtension(target, "xopp");

    h.prepareSave(doc, target);
    doc->unlock_shared();

    auto const createBackup = doc->shouldCreateBackupOnSave();

    if (createBackup) {
        try {
            // Note: The backup must be created for the target as this is the filepath
            // which will be written to. Do not use the `filepath` variable!
            Util::safeRenameFile(target, fs::path{target} += "~");
        } catch (const fs::filesystem_error& fe) {
            g_warning("Could not create backup! Failed with %s", fe.what());
            this->lastError = FS(_F("Save file error, can't backup: {1}") % std::string(fe.what()));
            if (!control->getWindow()) {
                g_error("%s", this->lastError.c_str());
            }
            return false;
        }
    }

    h.saveTo(target, this->control);

    doc->lock();
    h.updateDocumentInfo(doc);
    doc->setFilepath(target);
    doc->unlock();

    if (!h.getErrorMessage().empty()) {
        this->lastError = FS(_F("Save file error: {1}") % h.getErrorMessage());
        if (!control->getWindow()) {
            g_error("%s", this->lastError.c_str());
        }
        return false;
    } else if (createBackup) {
        try {
            // If a backup was created it can be removed now since no error occured during the save
            fs::remove(fs::path{target} += "~");
        } catch (const fs::filesystem_error& fe) {
            g_warning("Could not delete backup! Failed with %s", fe.what());
        }
    } else {
        doc->setCreateBackupOnSave(true);
    }

    // ---- COBBLE CLOUD SYNC ----
    // save() runs on BlockingJob worker thread — GTK main loop is still live.
    // We use g_idle_add + std::promise/future to safely orchestrate UI updates.
    std::string filePath = target.string();
    std::string fileName = target.filename().string();

    // Guard: only sync genuine user saves.
    // Skip autosaves (.autosave.xopp), backups (.xopp~), and anything
    // that is not a plain .xopp file.
    bool isGenuineSave =
        fileName.size() >= 5 &&
        fileName.substr(fileName.size() - 5) == ".xopp" &&
        fileName.find(".autosave") == std::string::npos &&
        fileName.find('~') == std::string::npos;

    if (!isGenuineSave) {
        return true;  // local save succeeded; skip cloud upload silently
    }

    const std::string supabaseUrl = "https://rcztclkkcpxsosptdgwe.supabase.co";
    const std::string anonKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InJjenRjbGtrY3B4c29zcHRkZ3dlIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODI5MjUxMzYsImV4cCI6MjA5ODUwMTEzNn0.HFdE91Yrc6__cnT3sR2mePPJTYPj6wVSfObUNXJ_gQM";

    GtkWindow* parentWin = control->getGtkWindow();

    // Step 1: Show spinner on GTK main thread. Wait until it appears.
    auto* sd = new CobbleSpinnerData{parentWin, {}};
    auto spinnerFuture = sd->dlgPromise.get_future();
    g_idle_add(cobble_show_spinner, sd);
    GtkWidget* spinnerDlg = spinnerFuture.get();  // blocks worker until dialog exists

    // Step 2: Run curl uploads synchronously on this worker thread (UI stays live).
    // --fail makes curl exit non-zero on any HTTP 4xx/5xx so we detect real failures.
    std::string uploadCmd =
        "curl -s --fail -X POST \"" + supabaseUrl + "/storage/v1/object/cobble_docs/" + fileName + "\" "
        "-H \"Authorization: Bearer " + anonKey + "\" "
        "-H \"apikey: " + anonKey + "\" "
        "-H \"Content-Type: application/octet-stream\" "
        "-H \"x-upsert: true\" "
        "--data-binary \"@" + filePath + "\"";
    int r1 = std::system(uploadCmd.c_str());

    std::string jsonPayload =
        "{\\\"filename\\\": \\\"" + fileName + "\\\", \\\"last_updated\\\": \\\"now()\\\"}";
    std::string metaCmd =
        "curl -s --fail -X POST \"" + supabaseUrl + "/rest/v1/cobble_metadata\" "
        "-H \"Authorization: Bearer " + anonKey + "\" "
        "-H \"apikey: " + anonKey + "\" "
        "-H \"Content-Type: application/json\" "
        "-H \"Prefer: resolution=merge-duplicates\" "
        "-d \"" + jsonPayload + "\"";
    int r2 = std::system(metaCmd.c_str());

    // Step 3: Close spinner and show result on GTK main thread.
    bool syncOk = (r1 == 0 && r2 == 0);
    g_idle_add(cobble_show_result, new CobbleResultData{spinnerDlg, parentWin, syncOk});
    // ---------------------------

    return true;
}
