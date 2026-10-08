#pragma once
#include "asset_runtime.hpp"
#include "material_grid.hpp"
#include <bgfx/bgfx.h>
#include <memory>
namespace lm {
// All GPU resources are owned on the preview thread and destroyed before bgfx.
class AssetRenderer {
public:
    explicit AssetRenderer(const CompiledAssets& assets);
    ~AssetRenderer();
    void sync_surfaces(const std::map<std::string,DynamicSurface>& surfaces);
    void draw(const std::vector<Entity>& entities, bgfx::ProgramHandle mesh_program, bool flat, const Vec3& eye);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
