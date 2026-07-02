#pragma once

#include <string>
#include <map>
#include <mutex>
#include <functional>

class Control;

namespace xoj {
namespace cobble {

enum class SyncState {
    Unsynced,
    Synced,
    Syncing,
    Failed
};

class CobbleSyncEngine {
public:
    static CobbleSyncEngine& getInstance() {
        static CobbleSyncEngine instance;
        return instance;
    }

    void setRootPath(const std::string& path);
    std::string getRootPath() const;

    // Triggers upload using GTK idle/background threads safely
    void uploadFileAsync(const std::string& absoluteFilePath, Control* control);

    SyncState getSyncState(const std::string& absoluteFilePath);
    
    // Pass an empty string to broadcast a general refresh
    using StateChangedCallback = std::function<void(const std::string& absoluteFilePath)>;
    void setListener(StateChangedCallback callback);

private:
    CobbleSyncEngine() = default;
    ~CobbleSyncEngine() = default;
    
    CobbleSyncEngine(const CobbleSyncEngine&) = delete;
    CobbleSyncEngine& operator=(const CobbleSyncEngine&) = delete;

    void setSyncState(const std::string& absoluteFilePath, SyncState state);
    
    void loadSyncStateDb();
    void saveSyncStateDb();

    std::string m_rootPath;
    std::map<std::string, SyncState> m_fileStates;
    std::map<std::string, long long> m_syncTimestamps;
    std::mutex m_stateMutex;
    StateChangedCallback m_listener;
};

} // namespace cobble
} // namespace xoj
