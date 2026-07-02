#pragma once

#include "gui/sidebar/AbstractSidebarPage.h"
#include <string>

namespace xoj {
namespace cobble {

class SidebarCobbleWorkspace : public AbstractSidebarPage {
public:
    SidebarCobbleWorkspace(Control* control);
    ~SidebarCobbleWorkspace() override;

    void enableSidebar() override;
    void disableSidebar() override;
    void layout() override;
    
    std::string getName() override;
    std::string getIconName() override;
    
    bool hasData() override;
    GtkWidget* getWidget() override;

private:
    GtkWidget* m_widget = nullptr;
    GtkWidget* m_treeView = nullptr;
    GtkTreeStore* m_treeStore = nullptr;
    GtkWidget* m_pathLabel = nullptr;
    
    guint m_pulseTimerId = 0;
    
    GdkPixbuf* m_iconSynced = nullptr;
    GdkPixbuf* m_iconError = nullptr;

    void populateTree();
    void populateNode(const std::string& currentPath, GtkTreeIter* parentIter);
    
    void updateFileState(const std::string& filePath);
    void updateFileStateRecursive(GtkTreeIter* iter, const std::string& filePath);
};

} // namespace cobble
} // namespace xoj
