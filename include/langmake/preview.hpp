#pragma once
#include "types.hpp"
#include <memory>
#include <functional>
union SDL_Event;
namespace lm {
struct PreviewOptions {
    int frames = 0;
    int width = 1280;
    int height = 800;
    // Zero uses the actual display density / platform safe area. Overrides make
    // desktop phone-layout previews reproducible without changing game content.
    float density = 0;
    std::array<int,4> safe_area{};
    bool audio = true;
    bool watch_source = true;
    bool overlay = true;
    bool paused = false;
    bool player = false;
    bool isolated = false;
    std::vector<std::string> debug;
    int steps = -1;
    Json timeline = Json::object();
    std::vector<int> capture_ticks;
    std::filesystem::path capture_directory;
    std::filesystem::path capture;
    std::filesystem::path executable_directory;
    // Platform adapters may keep writable state separate from packaged sources.
    // Empty preserves the desktop project/.langmake convention.
    std::filesystem::path storage_directory;
    bool restore_recovery = false;
    // Native platform transport may atomically commit completed inbox uploads.
    // Never exposed to scripts or serialized game project configuration.
    std::function<void(const std::filesystem::path&)> prepare_commands;
};
// Main-thread callback adapter. Each iterate returns control to the platform
// after one rendered frame; event() replaces SDL_PollEvent in callback mode.
// The blocking CLI wrapper below drives the exact same session.
class PreviewSession {
public:
    PreviewSession(Project project, PreviewOptions options, bool callback_events = false);
    ~PreviewSession();
    PreviewSession(const PreviewSession&) = delete;
    PreviewSession& operator=(const PreviewSession&) = delete;
    bool iterate();
    void event(const SDL_Event& event);
    void stop();
    Json result() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Json preview(Project project, const PreviewOptions& options);
void write_json_atomic(const std::filesystem::path& path, const Json& value);
}
