#include "langmake/assets.hpp"
#include "langmake/model.hpp"

#include <lodepng/lodepng.h>
#include <manifold/manifold.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <stdexcept>

namespace lm {
namespace {
constexpr std::size_t max_sprite_pixels = 4 * 1024 * 1024;
constexpr std::size_t max_mesh_vertices = 100000;
constexpr std::size_t max_geometry_nodes = 256;
constexpr int max_geometry_depth = 16;

[[noreturn]] void fail(const std::string& where, const std::string& message) {
    throw std::runtime_error("Asset " + where + ": " + message);
}
void object(const Json& value, const std::string& where) {
    if (!value.is_object()) fail(where, "expected an object");
}
void fields(const Json& value, std::initializer_list<const char*> allowed, const std::string& where) {
    object(value, where);
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* key) { return it.key() == key; }))
            fail(where + "." + it.key(), "unknown field");
    }
}
const Json& required(const Json& value, const char* key, const std::string& where) {
    if (!value.contains(key)) fail(where + "." + key, "required field is missing");
    return value.at(key);
}
double number(const Json& value, double low, double high, const std::string& where) {
    if (!value.is_number()) fail(where, "expected a finite number");
    const double result = value.get<double>();
    if (!std::isfinite(result) || result < low || result > high)
        fail(where, "number must be in [" + std::to_string(low) + ", " + std::to_string(high) + "]");
    return result;
}
int integer(const Json& value, int low, int high, const std::string& where) {
    if (!value.is_number_integer()) fail(where, "expected an integer");
    const double result = number(value, low, high, where);
    return static_cast<int>(result);
}
std::string text(const Json& value, const std::string& where) {
    if (!value.is_string()) fail(where, "expected text");
    const auto result = value.get<std::string>();
    if (result.empty() || result.size() > 128) fail(where, "text must contain 1..128 characters");
    return result;
}
std::string identifier(const Json& value, const std::string& where) {
    auto result = text(value, where);
    for (unsigned char c : result)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_' && c != '-')
            fail(where, "identifier permits letters, numbers, underscore and hyphen");
    return result;
}
void version(const Json& value, const std::string& where) {
    integer(required(value, "schema_version", where), 1, 1, where + ".schema_version");
}
std::array<unsigned char, 4> rgba(const Json& value, const std::string& where) {
    const auto s = text(value, where);
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#') fail(where, "color must be #RRGGBB or #RRGGBBAA");
    auto nibble = [&](char c) -> unsigned char {
        if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
        fail(where, "color contains a non-hexadecimal digit");
    };
    std::array<unsigned char, 4> result{0, 0, 0, 255};
    for (std::size_t i = 0; i < (s.size() - 1) / 2; ++i) result[i] = static_cast<unsigned char>(16 * nibble(s[1 + i * 2]) + nibble(s[2 + i * 2]));
    return result;
}
using Palette = std::map<char, std::array<unsigned char, 4>>;
std::array<unsigned char, 4> draw_color(const Json& value, const Palette& palette, const std::string& where) {
    if (value.is_string()) {
        const auto s = value.get<std::string>();
        if (s.size() == 1) {
            const auto found = palette.find(s[0]);
            if (found == palette.end()) fail(where, "unknown palette character");
            return found->second;
        }
    }
    return rgba(value, where);
}
void pixel(SpriteFrame& frame, int x, int y, const std::array<unsigned char, 4>& color) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) return;
    const auto at = (static_cast<std::size_t>(y) * frame.width + x) * 4;
    std::copy(color.begin(), color.end(), frame.rgba.begin() + at);
}
void raster(SpriteFrame& frame, const Json& operations, const Palette& palette, std::size_t& work, const std::string& where) {
    if (!operations.is_array() || operations.size() > 64) fail(where, "draw must contain at most 64 operations");
    auto spend = [&](std::size_t count) { work += count; if (work > 64 * 1024 * 1024) fail(where, "drawing exceeds the 64-million-pixel work budget"); };
    for (std::size_t i = 0; i < operations.size(); ++i) {
        const auto& op = operations[i]; const auto path = where + "[" + std::to_string(i) + "]";
        object(op, path);
        const auto kind = text(required(op, "op", path), path + ".op");
        const auto color = draw_color(required(op, "color", path), palette, path + ".color");
        auto coord = [&](const char* key) { return integer(required(op, key, path), -4096, 4096, path + "." + key); };
        if (kind == "rect") {
            fields(op, {"op", "x", "y", "width", "height", "color"}, path);
            int x = coord("x"), y = coord("y");
            int w = integer(required(op, "width", path), 1, 4096, path + ".width");
            int h = integer(required(op, "height", path), 1, 4096, path + ".height");
            const int x0 = std::max(0, x), x1 = std::min(frame.width, x + w), y0 = std::max(0, y), y1 = std::min(frame.height, y + h);
            spend(static_cast<std::size_t>(std::max(0, x1 - x0)) * std::max(0, y1 - y0));
            for (int row = y0; row < y1; ++row) for (int column = x0; column < x1; ++column) pixel(frame, column, row, color);
        } else if (kind == "circle") {
            fields(op, {"op", "cx", "cy", "radius", "color"}, path);
            int cx = coord("cx"), cy = coord("cy"), r = integer(required(op, "radius", path), 0, 2048, path + ".radius");
            const int x0 = std::max(0, cx - r), x1 = std::min(frame.width - 1, cx + r), y0 = std::max(0, cy - r), y1 = std::min(frame.height - 1, cy + r);
            spend(static_cast<std::size_t>(std::max(0, x1 - x0 + 1)) * std::max(0, y1 - y0 + 1));
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) pixel(frame, x, y, color);
        } else if (kind == "line") {
            fields(op, {"op", "x1", "y1", "x2", "y2", "thickness", "color"}, path);
            int x = coord("x1"), y = coord("y1"), x2 = coord("x2"), y2 = coord("y2");
            const int width = op.contains("thickness") ? integer(op["thickness"], 1, 32, path + ".thickness") : 1;
            const int dx = std::abs(x2 - x), dy = -std::abs(y2 - y), sx = x < x2 ? 1 : -1, sy = y < y2 ? 1 : -1;
            int error = dx + dy;
            spend(static_cast<std::size_t>(std::max(dx, -dy) + 1) * width * width);
            for (;;) {
                for (int yy = -width / 2; yy < width - width / 2; ++yy)
                    for (int xx = -width / 2; xx < width - width / 2; ++xx) pixel(frame, x + xx, y + yy, color);
                if (x == x2 && y == y2) break;
                const int twice = error * 2;
                if (twice >= dy) { error += dy; x += sx; }
                if (twice <= dx) { error += dx; y += sy; }
            }
        } else fail(path + ".op", "expected rect, circle or line");
    }
}
manifold::vec3 vector3(const Json& value, double low, double high, const std::string& where) {
    if (!value.is_array() || value.size() != 3) fail(where, "expected three numbers");
    return {number(value[0], low, high, where + "[0]"), number(value[1], low, high, where + "[1]"), number(value[2], low, high, where + "[2]")};
}
void mesh_budget(const manifold::Manifold& solid, const std::string& where) {
    if (solid.Status() != manifold::Manifold::Error::NoError) fail(where, "geometry construction failed with Manifold status " + std::to_string(static_cast<int>(solid.Status())));
    if (solid.NumTri() > max_mesh_vertices / 3) fail(where, "intermediate geometry exceeds 33333 triangles");
    if (solid.NumTri() != 0) {
        const auto bounds = solid.BoundingBox();
        for (double value : {bounds.min.x, bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z})
            if (!std::isfinite(value) || std::abs(value) > 100000)
                fail(where, "intermediate coordinate exceeds finite +/-100000 bounds");
    }
}
manifold::Manifold geometry(const Json& node, std::size_t& nodes, int depth, const std::string& where) {
    if (++nodes > max_geometry_nodes) fail(where, "recipe exceeds 256 geometry nodes");
    if (depth > max_geometry_depth) fail(where, "geometry nesting exceeds 16 levels");
    object(node, where);
    const auto op = text(required(node, "op", where), where + ".op");
    manifold::Manifold result;
    const auto scalar = [&](const char* key) { return number(required(node, key, where), 0.001, 1000, where + "." + key); };
    const auto segments = [&]() { return node.contains("segments") ? integer(node["segments"], 4, 64, where + ".segments") : 24; };
    if (op == "box") {
        fields(node, {"op", "size", "translate", "rotate", "scale"}, where);
        result = manifold::Manifold::Cube(vector3(required(node, "size", where), 0.001, 1000, where + ".size"), true);
    } else if (op == "sphere") {
        fields(node, {"op", "radius", "segments", "translate", "rotate", "scale"}, where);
        result = manifold::Manifold::Sphere(scalar("radius"), segments());
    } else if (op == "cylinder" || op == "cone") {
        fields(node, {"op", "radius", "height", "segments", "translate", "rotate", "scale"}, where);
        const double radius = scalar("radius");
        result = manifold::Manifold::Cylinder(scalar("height"), radius, op == "cone" ? 0 : radius, segments(), true).Rotate(-90, 0, 0);
    } else if (op == "union" || op == "difference" || op == "intersection") {
        fields(node, {"op", "children", "translate", "rotate", "scale"}, where);
        const auto& children = required(node, "children", where);
        if (!children.is_array() || children.size() < 2 || children.size() > 32) fail(where + ".children", "CSG requires 2..32 children");
        result = geometry(children[0], nodes, depth + 1, where + ".children[0]");
        for (std::size_t i = 1; i < children.size(); ++i) {
            auto child = geometry(children[i], nodes, depth + 1, where + ".children[" + std::to_string(i) + "]");
            result = result.Boolean(child, op == "union" ? manifold::OpType::Add : op == "difference" ? manifold::OpType::Subtract : manifold::OpType::Intersect);
            mesh_budget(result, where);
        }
    } else fail(where + ".op", "expected box, sphere, cylinder, cone, union, difference or intersection");
    if (node.contains("scale")) result = result.Scale(vector3(node["scale"], 0.001, 1000, where + ".scale"));
    if (node.contains("rotate")) { auto r = vector3(node["rotate"], -36000, 36000, where + ".rotate"); result = result.Rotate(r.x, r.y, r.z); }
    if (node.contains("translate")) result = result.Translate(vector3(node["translate"], -10000, 10000, where + ".translate"));
    mesh_budget(result, where);
    return result;
}
void output_parent(const std::filesystem::path& output) {
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
}
}

SpriteAsset compile_sprite(const AssetSource& source) {
    const auto& doc = source.document;
    fields(doc, {"schema_version", "description", "width", "height", "palette", "source_png", "frames", "animations", "default_animation"}, "sprite");
    version(doc, "sprite");
    const int width = integer(required(doc, "width", "sprite"), 1, 1024, "sprite.width");
    const int height = integer(required(doc, "height", "sprite"), 1, 1024, "sprite.height");
    Palette palette;
    if (doc.contains("palette")) {
        object(doc["palette"], "sprite.palette");
        for (auto it = doc["palette"].begin(); it != doc["palette"].end(); ++it) {
            if (it.key().size() != 1 || static_cast<unsigned char>(it.key()[0]) < 32 || static_cast<unsigned char>(it.key()[0]) > 126)
                fail("sprite.palette", "keys must be single printable ASCII characters");
            palette[it.key()[0]] = rgba(it.value(), "sprite.palette." + it.key());
        }
    }
    std::vector<unsigned char> png;
    unsigned png_width = 0, png_height = 0;
    if (doc.contains("source_png")) {
        const auto path = text(doc["source_png"], "sprite.source_png");
        const auto found = source.inputs.find(path);
        if (found == source.inputs.end()) fail("sprite.source_png", "input bytes were not provided for " + path);
        if (found->second.size() > 64 * 1024 * 1024) fail("sprite.source_png", "encoded PNG exceeds 64 MiB");
        lodepng::State state;
        state.decoder.read_text_chunks = 0;
        state.decoder.max_icc_size = 1024 * 1024;
        state.decoder.zlibsettings.max_output_size = 129 * 1024 * 1024;
        auto bytes = reinterpret_cast<const unsigned char*>(found->second.data());
        auto error = lodepng_inspect(&png_width, &png_height, &state, bytes, found->second.size());
        if (error) fail("sprite.source_png", lodepng_error_text(error));
        if (!png_width || !png_height || png_width > 4096 || png_height > 4096) fail("sprite.source_png", "PNG dimensions must be 1..4096");
        error = lodepng::decode(png, png_width, png_height, state, bytes, found->second.size());
        if (error) fail("sprite.source_png", lodepng_error_text(error));
    }
    const auto& frames = required(doc, "frames", "sprite");
    if (!frames.is_array() || frames.empty() || frames.size() > 64) fail("sprite.frames", "expected 1..64 frames");
    if (static_cast<std::size_t>(width) * height * frames.size() > max_sprite_pixels) fail("sprite.frames", "total frame pixels exceed 4194304");
    SpriteAsset sprite;
    std::map<std::string, std::size_t> ids;
    std::size_t work = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& definition = frames[i]; const auto where = "sprite.frames[" + std::to_string(i) + "]";
        fields(definition, {"id", "pixels", "draw", "source_rect"}, where);
        SpriteFrame frame; frame.id = identifier(required(definition, "id", where), where + ".id");
        if (!ids.emplace(frame.id, i).second) fail(where + ".id", "duplicate frame ID");
        frame.width = width; frame.height = height; frame.rgba.resize(static_cast<std::size_t>(width) * height * 4, 0);
        if (!definition.contains("pixels") && !definition.contains("draw") && !definition.contains("source_rect")) fail(where, "frame requires pixels, draw or source_rect");
        if (definition.contains("source_rect")) {
            if (png.empty()) fail(where + ".source_rect", "source_png must be provided");
            const auto& rect = definition["source_rect"];
            if (!rect.is_array() || rect.size() != 4) fail(where + ".source_rect", "expected [x,y,width,height]");
            const int x = integer(rect[0], 0, static_cast<int>(png_width), where + ".source_rect[0]");
            const int y = integer(rect[1], 0, static_cast<int>(png_height), where + ".source_rect[1]");
            if (integer(rect[2], 1, 1024, where + ".source_rect[2]") != width || integer(rect[3], 1, 1024, where + ".source_rect[3]") != height)
                fail(where + ".source_rect", "crop dimensions must match the frame dimensions");
            if (x + width > static_cast<int>(png_width) || y + height > static_cast<int>(png_height)) fail(where + ".source_rect", "crop lies outside PNG");
            for (int row = 0; row < height; ++row) std::copy_n(png.begin() + (static_cast<std::size_t>(y + row) * png_width + x) * 4, width * 4, frame.rgba.begin() + static_cast<std::size_t>(row) * width * 4);
        }
        if (definition.contains("pixels")) {
            const auto& rows = definition["pixels"];
            if (!rows.is_array() || rows.size() != static_cast<std::size_t>(height)) fail(where + ".pixels", "row count must match height");
            for (int y = 0; y < height; ++y) {
                if (!rows[y].is_string()) fail(where + ".pixels", "each row must be text");
                const auto row = rows[y].get<std::string>();
                if (row.size() != static_cast<std::size_t>(width)) fail(where + ".pixels[" + std::to_string(y) + "]", "row length must match width");
                for (int x = 0; x < width; ++x) {
                    const auto color = palette.find(row[x]);
                    if (color == palette.end()) fail(where + ".pixels[" + std::to_string(y) + "]", "unknown palette character at column " + std::to_string(x));
                    pixel(frame, x, y, color->second);
                }
            }
        }
        if (definition.contains("draw")) raster(frame, definition["draw"], palette, work, where + ".draw");
        sprite.frames.push_back(std::move(frame));
    }
    if (doc.contains("animations")) {
        const auto& clips = doc["animations"]; object(clips, "sprite.animations");
        if (clips.empty() || clips.size() > 64) fail("sprite.animations", "expected 1..64 animations");
        for (auto it = clips.begin(); it != clips.end(); ++it) {
            const auto name = identifier(it.key(), "sprite.animations name"); const auto where = "sprite.animations." + name;
            const auto& clip = it.value(); fields(clip, {"frames", "durations", "loop"}, where);
            const auto& frame_ids = required(clip, "frames", where); const auto& times = required(clip, "durations", where);
            if (!frame_ids.is_array() || frame_ids.empty() || frame_ids.size() > 256 || !times.is_array() || times.size() != frame_ids.size())
                fail(where, "frames and durations must be matching arrays with 1..256 entries");
            SpriteClip result;
            if (clip.contains("loop")) { if (!clip["loop"].is_boolean()) fail(where + ".loop", "expected boolean"); result.loop = clip["loop"].get<bool>(); }
            for (std::size_t i = 0; i < frame_ids.size(); ++i) {
                const auto name = text(frame_ids[i], where + ".frames[" + std::to_string(i) + "]");
                const auto found = ids.find(name); if (found == ids.end()) fail(where + ".frames", "unknown frame ID " + name);
                result.frames.push_back(found->second); result.durations.push_back(number(times[i], 0.01, 60, where + ".durations[" + std::to_string(i) + "]"));
            }
            sprite.animations.emplace(name, std::move(result));
        }
    } else {
        SpriteClip clip;
        for (std::size_t i = 0; i < sprite.frames.size(); ++i) { clip.frames.push_back(i); clip.durations.push_back(0.1); }
        sprite.animations.emplace("default", std::move(clip));
    }
    sprite.default_animation = doc.contains("default_animation") ? identifier(doc["default_animation"], "sprite.default_animation") : sprite.animations.begin()->first;
    if (!sprite.animations.contains(sprite.default_animation)) fail("sprite.default_animation", "unknown animation name");
    return sprite;
}

std::size_t sprite_frame_at(const SpriteAsset& sprite, const std::string& requested, double seconds) {
    const auto name = requested.empty() ? sprite.default_animation : requested;
    const auto found = sprite.animations.find(name);
    if (found == sprite.animations.end()) fail("sprite.animation", "unknown animation " + name);
    const auto& clip = found->second;
    if (clip.frames.empty() || clip.frames.size() != clip.durations.size()) fail("sprite.animation", "invalid compiled clip");
    if (!std::isfinite(seconds)) fail("sprite.animation", "time must be finite");
    double duration = 0;
    for (double value : clip.durations) { if (!std::isfinite(value) || value <= 0) fail("sprite.animation", "invalid compiled duration"); duration += value; }
    seconds = std::max(0.0, seconds);
    if (clip.loop) seconds = std::fmod(seconds, duration);
    else if (seconds >= duration) return clip.frames.back();
    for (std::size_t i = 0; i < clip.frames.size(); ++i) { if (seconds < clip.durations[i]) return clip.frames[i]; seconds -= clip.durations[i]; }
    return clip.frames.back();
}

MeshAsset compile_mesh(const AssetSource& source) {
    const auto& doc = source.document;
    fields(doc, {"schema_version", "description", "parts"}, "mesh"); version(doc, "mesh");
    const auto& parts = required(doc, "parts", "mesh");
    if (!parts.is_array() || parts.empty() || parts.size() > 64) fail("mesh.parts", "expected 1..64 named parts");
    MeshAsset mesh; std::set<std::string> names; std::size_t nodes = 0;
    for (std::size_t p = 0; p < parts.size(); ++p) {
        const auto& part = parts[p]; const auto where = "mesh.parts[" + std::to_string(p) + "]";
        fields(part, {"id", "color", "geometry"}, where);
        const auto name = identifier(required(part, "id", where), where + ".id");
        if (!names.insert(name).second) fail(where + ".id", "duplicate part ID");
        const auto bytes = rgba(required(part, "color", where), where + ".color");
        if (bytes[3] != 255) fail(where + ".color", "mesh parts must be opaque (alpha FF); sprite assets support transparency");
        Color color{}; for (int channel = 0; channel < 4; ++channel) color[channel] = bytes[channel] / 255.0f;
        const auto solid = geometry(required(part, "geometry", where), nodes, 0, where + ".geometry");
        auto triangles = solid.GetMeshGL();
        if (triangles.triVerts.empty()) fail(where + ".geometry", "part has empty geometry");
        if (mesh.vertices.size() + triangles.triVerts.size() > max_mesh_vertices) fail(where, "compiled mesh exceeds 100000 vertices");
        mesh.parts.push_back(name); mesh.part_index_offsets.push_back(mesh.indices.size());
        for (std::size_t triangle = 0; triangle < triangles.triVerts.size(); triangle += 3) {
            std::array<Vec3, 3> positions;
            for (int corner = 0; corner < 3; ++corner) {
                const std::size_t index = static_cast<std::size_t>(triangles.triVerts[triangle + corner]) * triangles.numProp;
                for (int axis = 0; axis < 3; ++axis) {
                    positions[corner][axis] = triangles.vertProperties.at(index + axis);
                    if (!std::isfinite(positions[corner][axis]) || std::abs(positions[corner][axis]) > 100000) fail(where, "output coordinate exceeds finite +/-100000 bounds");
                }
            }
            const auto& a = positions[0]; const auto& b = positions[1]; const auto& c = positions[2];
            const std::array<double,3> u{double(b[0])-a[0],double(b[1])-a[1],double(b[2])-a[2]}, v{double(c[0])-a[0],double(c[1])-a[1],double(c[2])-a[2]};
            const std::array<double,3> cross{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
            const double length = std::sqrt(cross[0]*cross[0] + cross[1]*cross[1] + cross[2]*cross[2]);
            if (!(length > 0)) continue;
            Vec3 normal{};
            for (int axis=0; axis<3; ++axis) normal[axis]=static_cast<float>(cross[axis]/length);
            for (const auto& position : positions) {
                if (mesh.vertices.empty()) { mesh.minimum = position; mesh.maximum = position; }
                for (int axis = 0; axis < 3; ++axis) { mesh.minimum[axis] = std::min(mesh.minimum[axis], position[axis]); mesh.maximum[axis] = std::max(mesh.maximum[axis], position[axis]); }
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back({position, normal, color});
            }
        }
        if (mesh.indices.size() == mesh.part_index_offsets.back()) fail(where, "part has no nondegenerate triangles");
    }
    mesh.part_index_offsets.push_back(mesh.indices.size());
    return mesh;
}

Json inspect_sprite(const SpriteAsset& sprite) {
    Json frames = Json::array(), clips = Json::object();
    for (const auto& frame : sprite.frames) frames.push_back({{"id", frame.id}, {"width", frame.width}, {"height", frame.height}});
    for (const auto& [name, clip] : sprite.animations) {
        Json ids = Json::array(); double duration = 0;
        for (std::size_t index : clip.frames) ids.push_back(sprite.frames.at(index).id);
        for (double time : clip.durations) duration += time;
        clips[name] = {{"frames", ids}, {"durations", clip.durations}, {"loop", clip.loop}, {"duration", duration}};
    }
    return {{"type", "sprite"}, {"frames", frames}, {"animations", clips}, {"default_animation", sprite.default_animation}};
}
Json inspect_mesh(const MeshAsset& mesh) {
    Json parts = Json::array();
    for (std::size_t i = 0; i < mesh.parts.size(); ++i) {
        Json part{{"id", mesh.parts[i]}};
        if (mesh.part_index_offsets.size() == mesh.parts.size() + 1) part["triangles"] = (mesh.part_index_offsets[i + 1] - mesh.part_index_offsets[i]) / 3;
        parts.push_back(part);
    }
    return {{"type", "mesh"}, {"vertices", mesh.vertices.size()}, {"triangles", mesh.indices.size()/3}, {"bounds", {{"minimum", mesh.minimum}, {"maximum", mesh.maximum}}}, {"parts", parts}, {"units", "meters"}, {"up_axis", "Y"}};
}
void write_sprite_sheet(const SpriteAsset& sprite, const std::filesystem::path& output, int scale) {
    if (sprite.frames.empty() || scale < 1 || scale > 8) fail("sprite export", "requires frames and an integer scale in 1..8");
    const int frame_width = sprite.frames.front().width, frame_height = sprite.frames.front().height;
    const std::size_t width = static_cast<std::size_t>(frame_width) * sprite.frames.size() * scale;
    const std::size_t height = static_cast<std::size_t>(frame_height) * scale;
    if (width == 0 || height == 0 || width > 32768 || height > 32768 || width * height > 16 * 1024 * 1024) fail("sprite export", "contact sheet exceeds dimension or 16-million-pixel limit");
    std::vector<unsigned char> image(width * height * 4);
    for (std::size_t frame = 0; frame < sprite.frames.size(); ++frame) {
        const auto& source = sprite.frames[frame];
        if (source.width != frame_width || source.height != frame_height || source.rgba.size() != static_cast<std::size_t>(frame_width)*frame_height*4) fail("sprite export", "compiled frames have inconsistent dimensions");
        for (int y = 0; y < frame_height * scale; ++y) for (int x = 0; x < frame_width * scale; ++x) {
            const auto from = (static_cast<std::size_t>(y/scale)*frame_width+x/scale)*4;
            const auto to = (static_cast<std::size_t>(y)*width+frame*frame_width*scale+x)*4;
            std::copy_n(source.rgba.begin()+from, 4, image.begin()+to);
        }
    }
    output_parent(output);
    const auto error = lodepng::encode(output.string(), image, static_cast<unsigned>(width), static_cast<unsigned>(height));
    if (error) fail("sprite export", lodepng_error_text(error));
}
void write_mesh_obj(const MeshAsset& mesh, const std::filesystem::path& output) {
    output_parent(output);
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) fail("mesh export", "cannot open output");
    stream << "# Langmake mesh: meters, Y up. Vertex colors use the common OBJ RGB extension.\n" << std::setprecision(9);
    for (const auto& v : mesh.vertices) stream << "v " << v.position[0] << ' ' << v.position[1] << ' ' << v.position[2] << ' ' << v.color[0] << ' ' << v.color[1] << ' ' << v.color[2] << '\n';
    for (const auto& v : mesh.vertices) stream << "vn " << v.normal[0] << ' ' << v.normal[1] << ' ' << v.normal[2] << '\n';
    std::size_t part = 0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        if (part < mesh.parts.size() && mesh.part_index_offsets.size() == mesh.parts.size()+1 && i == mesh.part_index_offsets[part]) stream << "g " << mesh.parts[part++] << '\n';
        stream << 'f'; for (int corner = 0; corner < 3; ++corner) { const auto index = mesh.indices.at(i+corner)+1; stream << ' ' << index << "//" << index; } stream << '\n';
    }
    if (!stream) fail("mesh export", "output write failed");
}

Json asset_schema(const std::string& type) {
    if (type == "model") return model_schema();
    Json color{{"type", "string"}, {"pattern", "^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$"}};
    Json id{{"type", "string"}, {"pattern", "^[A-Za-z0-9_-]{1,128}$"}};
    if (type == "sprite") {
        auto integer_schema=[](int minimum,int maximum){ return Json{{"type","integer"},{"minimum",minimum},{"maximum",maximum}}; };
        Json draw_variants=Json::array();
        for(const std::string kind:{"rect","circle","line"}) {
            Json op{{"type","object"},{"additionalProperties",false},{"required",{"op","color"}},
                {"properties",{{"op",{{"const",kind}}},{"color",{{"type","string"},{"pattern","^([ -~]|#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?)$"}}}}}};
            const std::vector<std::string> coordinates=kind=="rect"?std::vector<std::string>{"x","y"}:
                kind=="circle"?std::vector<std::string>{"cx","cy"}:std::vector<std::string>{"x1","y1","x2","y2"};
            for(const auto& name:coordinates) { op["properties"][name]=integer_schema(-4096,4096); op["required"].push_back(name); }
            if(kind=="rect") for(const std::string name:{"width","height"}) { op["properties"][name]=integer_schema(1,4096); op["required"].push_back(name); }
            if(kind=="circle") { op["properties"]["radius"]=integer_schema(0,2048); op["required"].push_back("radius"); }
            if(kind=="line") { op["properties"]["thickness"]=integer_schema(1,32); op["properties"]["thickness"]["default"]=1; }
            draw_variants.push_back(std::move(op));
        }
        Json frame{{"type", "object"}, {"additionalProperties", false}, {"required", {"id"}}, {"properties", {
            {"id", id}, {"pixels", {{"type", "array"}, {"items", {{"type", "string"}}}}},
            {"source_rect", {{"type", "array"}, {"minItems", 4}, {"maxItems", 4}, {"items", {{"type", "integer"}, {"minimum", 0}}}}},
            {"draw", {{"type", "array"}, {"maxItems", 64}, {"items", {{"oneOf",draw_variants}}}}}
        }}, {"anyOf", Json::array({{{"required", {"pixels"}}}, {{"required", {"draw"}}}, {{"required", {"source_rect"}}}})}};
        Json clip{{"type", "object"}, {"additionalProperties", false}, {"required", {"frames", "durations"}}, {"properties", {
            {"frames", {{"type", "array"}, {"minItems", 1}, {"maxItems", 256}, {"items", id}}},
            {"durations", {{"type", "array"}, {"minItems", 1}, {"maxItems", 256}, {"items", {{"type", "number"}, {"minimum", 0.01}, {"maximum", 60}}}}}, {"loop", {{"type", "boolean"}}}
        }}};
        return {{"$schema", "https://json-schema.org/draft/2020-12/schema"}, {"title", "Langmake sprite recipe v1"}, {"type", "object"}, {"additionalProperties", false}, {"required", {"schema_version", "width", "height", "frames"}}, {"properties", {
            {"schema_version", {{"const", 1}}}, {"description", {{"type", "string"}}}, {"width", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1024}}}, {"height", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1024}}},
            {"palette", {{"type", "object"}, {"propertyNames", {{"pattern", "^[ -~]$"}}}, {"additionalProperties", color}}}, {"source_png", {{"type", "string"}}}, {"frames", {{"type", "array"}, {"minItems", 1}, {"maxItems", 64}, {"items", frame}}}, {"animations", {{"type", "object"}, {"minProperties", 1}, {"maxProperties", 64}, {"propertyNames",id}, {"additionalProperties", clip}}}, {"default_animation", id}
        }}, {"description", "Pixel origin top-left, RGBA hex palette, seconds per animation frame. Draw order: source_rect, pixels, draw. Draw operations replace RGBA; no implicit blending. Missing animations synthesize default (all frames, 0.1 seconds each). Missing default_animation selects the first animation alphabetically. Compiler checks references, exact row/crop sizes and a 4194304 total-frame-pixel budget."}};
    }
    if (type == "mesh") {
        auto vector=[](double minimum,double maximum){ return Json{{"type","array"},{"minItems",3},{"maxItems",3},{"items",{{"type","number"},{"minimum",minimum},{"maximum",maximum}}}}; };
        Json variants=Json::array();
        for(const std::string kind:{"box","sphere","cylinder","cone","union","difference","intersection"}) {
            Json node{{"type","object"},{"additionalProperties",false},{"required",{"op"}},
                {"properties",{{"op",{{"const",kind}}},{"translate",vector(-10000,10000)},{"rotate",vector(-36000,36000)},{"scale",vector(.001,1000)}}}};
            if(kind=="box") { node["properties"]["size"]=vector(.001,1000); node["required"].push_back("size"); }
            else if(kind=="sphere"||kind=="cylinder"||kind=="cone") {
                node["properties"]["radius"]={{"type","number"},{"minimum",.001},{"maximum",1000}};
                node["properties"]["segments"]={{"type","integer"},{"minimum",4},{"maximum",64},{"default",24}};
                node["required"].push_back("radius");
                if(kind!="sphere") { node["properties"]["height"]=node["properties"]["radius"]; node["required"].push_back("height"); }
            } else {
                node["properties"]["children"]={{"type","array"},{"minItems",2},{"maxItems",32},{"items",{{"$ref","#/$defs/geometry"}}}};
                node["required"].push_back("children");
            }
            variants.push_back(std::move(node));
        }
        Json node{{"oneOf",variants}};
        Json opaque_color{{"type", "string"}, {"pattern", "^#[0-9a-fA-F]{6}([fF]{2})?$"}};
        Json part{{"type", "object"}, {"additionalProperties", false}, {"required", {"id", "color", "geometry"}},
            {"properties", {{"id", id}, {"color", opaque_color}, {"geometry", {{"$ref", "#/$defs/geometry"}}}}}};
        Json result{{"$schema", "https://json-schema.org/draft/2020-12/schema"}, {"title", "Langmake mesh recipe v1"},
            {"type", "object"}, {"additionalProperties", false}, {"required", {"schema_version", "parts"}}, {"$defs", {{"geometry", node}}}};
        result["properties"] = {{"schema_version", {{"const", 1}}}, {"description", {{"type", "string"}}},
            {"parts", {{"type", "array"}, {"minItems", 1}, {"maxItems", 64}, {"items", part}}}};
        result["description"] = "Meters, Y up. Centered primitives; cones point +Y. Nodes apply positive scale, XYZ rotation in degrees, then translation. Difference subtracts all later children from the first. Flat normals and opaque per-part colors (#RRGGBB or #RRGGBBFF). Maximum 256 nodes, depth 16 and 100000 output vertices.";
        return result;
    }
    fail("schema", "expected sprite or mesh asset type");
}
}
