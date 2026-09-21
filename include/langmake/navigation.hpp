#pragma once
#include "types.hpp"
#include <memory>

namespace lm {
// Recast/Detour owns only navigation data. Entity identities and movement belong to World.
class Navigation {
public:
    Navigation(const std::string& dimension, const std::vector<Entity>& geometry, const Json& settings);
    ~Navigation();
    Navigation(const Navigation&) = delete;
    Navigation& operator=(const Navigation&) = delete;
    Json path(const Vec3& from, const Vec3& to) const;
    Json inspect() const;
    // Orders map IDs to {position,goal,speed}; result maps IDs to velocity/status.
    Json steer(const Json& orders, float dt);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
