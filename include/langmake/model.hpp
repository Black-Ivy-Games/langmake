#pragma once
#include "assets.hpp"
#include <memory>

namespace lm {
using Matrix4 = std::array<float, 16>; // Column-major, meters, Y up.
struct ModelJoint {
    std::string id;
    int parent = -1;
    Vec3 translation{0,0,0}, scale{1,1,1};
    std::array<float,4> rotation{0,0,0,1}; // Quaternion xyzw.
};
struct ModelVertex {
    Vec3 position{}, normal{0,1,0};
    Color color{1,1,1,1};
    std::array<float,2> uv{};
    std::array<std::uint16_t,4> joints{};
    std::array<float,4> weights{1,0,0,0};
};
struct ModelMaterial {
    Color color{1,1,1,1};
    int image = -1;
    bool unlit = false, double_sided = false;
    std::string alpha_mode = "OPAQUE";
    float alpha_cutoff = .5f;
};
struct ModelPrimitive {
    std::string id;
    std::vector<ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Empty means rigid attachment; otherwise one inverse bind per model joint.
    std::vector<Matrix4> inverse_bind;
    std::size_t material = 0;
};
struct ModelMarker { std::string name; double time = 0; };
struct ModelClip { double duration = 1; bool loop = true; std::vector<ModelMarker> markers; };
struct ModelAnimationData;
struct ModelAsset {
    std::vector<ModelJoint> joints;
    std::vector<ModelPrimitive> primitives;
    std::vector<ModelMaterial> materials;
    std::vector<SpriteFrame> images;
    std::map<std::string,ModelClip> animations;
    std::string default_animation;
    Vec3 minimum{}, maximum{};
    bool imported = false;
    std::size_t animation_keys = 0;
    std::shared_ptr<const ModelAnimationData> runtime;
};
struct ModelPose { std::vector<Matrix4> joints; };
// Returns project-relative input paths, including the main source. Repeat after
// supplying missing inputs; compilers never open files or resolve network URIs.
std::vector<std::string> model_dependencies(const AssetSource& source);
ModelAsset compile_model(const AssetSource& source);
Json model_schema();
Json inspect_model(const ModelAsset& model);
class ModelSampler {
public:
    explicit ModelSampler(const ModelAsset& model);
    ~ModelSampler();
    ModelPose sample(const std::string& clip, double seconds,
        const std::string& previous_clip = "", double previous_seconds = 0, float blend = 1);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
ModelPose sample_model_pose(const ModelAsset& model, const std::string& clip, double seconds,
    const std::string& previous_clip = "", double previous_seconds = 0, float blend = 1);
Matrix4 model_joint_transform(const ModelAsset& model, const ModelPose& pose, const std::string& joint);
// Markers crossed in (from,to], including repeated loops; bounded to 128 results.
Json model_markers(const ModelAsset& model, const std::string& clip, double from, double to);
std::vector<ModelVertex> deform_model_primitive(const ModelPrimitive& primitive, const ModelPose& pose);
MeshAsset model_mesh_at(const ModelAsset& model, const std::string& clip = "", double seconds = 0);
}
