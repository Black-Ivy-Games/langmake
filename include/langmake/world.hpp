#pragma once
#include "types.hpp"
#include "asset_runtime.hpp"
#include <memory>
#include <functional>

namespace lm {
class World {
public:
    explicit World(const Project& project);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    void step(float dt, const Input& input);
    std::vector<Entity> entities() const;
    Json snapshot() const;
    bool won() const;
    int collected() const;
    std::string message() const;
    const CompiledAssets& assets() const;
    double time() const;
    Json audio_events() const;
    // Player save data is distinct from source checkpoints. Restore is transactional.
    Json save() const;
    void restore(const Json& save);
    void change_scene(const std::string& scene);
    // Prepare presentation resources before committing a CPU-world replacement.
    // Throwing leaves the existing world intact; callers stage their own resources.
    void set_scene_prepare(std::function<void(const Project&, const CompiledAssets&)> prepare);
    Json statistics() const;
    Json take_requests();
    const Project& current_project() const;
    Json presentation() const;
    // Host presentation metrics are transient and unavailable during initial start().
    // An empty safe area selects the full pixel viewport. Does not alter gameplay geometry.
    void set_viewport(int width, int height, float density, const std::array<int,4>& safe_area = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::function<void(const Project&, const CompiledAssets&)> scene_prepare_;
};
}
