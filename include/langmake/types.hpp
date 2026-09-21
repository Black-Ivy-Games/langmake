#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>

namespace lm {
using Json = nlohmann::json;
using Vec3 = std::array<float, 3>;
using Color = std::array<float, 4>;

struct Entity {
    Json physics2d = Json::object();
    std::string id;
    std::string name;
    std::string shape = "box";
    std::string body = "static";
    std::string role = "decoration";
    Vec3 position{0, 0, 0};
    Vec3 size{1, 1, 1};
    Color color{0.5f, 0.7f, 0.9f, 1};
    float mass = 1;
    bool visible = true;
    std::string asset;
    std::string animation;
    Vec3 rotation{0, 0, 0};
    bool flip_x = false;
    float animation_speed = 1;
    double animation_time = 0;
    Vec3 visual_scale{1, 1, 1};
    bool collision_enabled = true;
    std::string controller;
    std::string animation_previous;
    double animation_previous_time = 0;
    float animation_blend = 1;
    std::string parent;
    std::string parent_joint;
};

struct AssetSource {
    std::string type;
    std::filesystem::path path;
    std::string bytes;
    Json document;
    std::map<std::string, std::string> inputs;
    std::string revision;
};

struct Project {
    std::filesystem::path root;
    std::filesystem::path file;
    Json document;
    std::vector<Entity> entities;
    std::string dimension;
    std::string title;
    std::string revision;
    std::string script_source;
    std::filesystem::path script_file;
    Vec3 gravity{0, -18, 0};
    Vec3 camera_position{12, 12, 18};
    Vec3 camera_target{0, 1, 0};
    std::map<std::string, AssetSource> assets;
    std::string music;
    std::map<std::string, std::string> modules;
    std::map<std::string, std::filesystem::path> module_files;
    Json prefabs = Json::object();
    std::map<std::string, std::shared_ptr<Project>> scenes;
    std::string scene_id = "main";
    std::map<std::string, std::string> sources;
    Json ui = Json::object();
    Json definitions = Json::object();
    std::string source_revision;
    std::string profile;
    Json resolved_profile = Json::object();
    Json application = Json::object();
    Json scenarios = Json::object();
    std::string save_fingerprint;
};

struct Input {
    float move_x = 0;
    float move_z = 0;
    bool jump = false;
    bool fire = false;
    std::map<std::string, float> actions;
    Json events = Json::array();
    std::array<float, 2> pointer{0, 0};
};
}
