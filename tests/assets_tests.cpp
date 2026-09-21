#include "langmake/assets.hpp"
#include <lodepng/lodepng.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using lm::Json;
int checks = 0;
void require(bool value, const std::string& message) {
    ++checks; if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F function, const std::string& message) {
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; }
    require(failed, message);
}
lm::AssetSource source(Json doc, const std::string& type = "sprite") {
    lm::AssetSource result; result.type = type; result.document = std::move(doc); return result;
}
Json sprite_fixture() {
    return {{"schema_version", 1}, {"width", 2}, {"height", 2}, {"palette", {{".", "#00000000"}, {"R", "#FF000080"}, {"G", "#00FF00"}}},
        {"frames", Json::array({{{"id", "red"}, {"pixels", {"R.", ".R"}}}, {{"id", "green"}, {"pixels", {"GG", "GG"}}}})},
        {"animations", {{"walk", {{"frames", {"red", "green"}}, {"durations", {0.25, 0.5}}, {"loop", true}}}, {"once", {{"frames", {"red", "green"}}, {"durations", {0.25, 0.5}}, {"loop", false}}}}}, {"default_animation", "walk"}};
}
Json box(double size = 2) { return {{"op", "box"}, {"size", {size, size, size}}}; }
Json mesh_fixture(Json geometry) {
    return {{"schema_version", 1}, {"parts", Json::array({{{"id", "solid"}, {"color", "#4080FFFF"}, {"geometry", std::move(geometry)}}})}};
}
double volume(const lm::MeshAsset& mesh) {
    double value = 0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]].position;
        const auto& b = mesh.vertices[mesh.indices[i+1]].position;
        const auto& c = mesh.vertices[mesh.indices[i+2]].position;
        value += a[0] * (b[1]*c[2] - b[2]*c[1]) + a[1] * (b[2]*c[0] - b[0]*c[2]) + a[2] * (b[0]*c[1] - b[1]*c[0]);
    }
    return value / 6;
}
lm::AssetSource recipe(const fs::path& path, const std::string& type) {
    std::ifstream stream(path); Json document; stream >> document; return source(document, type);
}
}

int main(int argc, char** argv) {
    const fs::path root = fs::weakly_canonical(argc > 1 ? fs::path(argv[1]) : fs::path(__FILE__).parent_path().parent_path());
    const fs::path output = root / ".cache" / ("asset-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto sprite = lm::compile_sprite(source(sprite_fixture()));
        require(sprite.frames.size() == 2 && sprite.frames[0].rgba.size() == 16, "Sprite dimensions/frames are incorrect");
        require(sprite.frames[0].rgba[0] == 255 && sprite.frames[0].rgba[3] == 128 && sprite.frames[0].rgba[7] == 0, "Palette RGBA decoding lost transparency");
        require(lm::sprite_frame_at(sprite, "", 0.249) == 0 && lm::sprite_frame_at(sprite, "walk", 0.25) == 1, "Animation frame boundary incorrect");
        require(lm::sprite_frame_at(sprite, "walk", 0.75) == 0 && lm::sprite_frame_at(sprite, "once", 200) == 1, "Looping/nonlooping animation incorrect");
        require(lm::sprite_frame_at(sprite, "walk", -3) == 0, "Negative animation time did not clamp");
        rejects([&] { lm::sprite_frame_at(sprite, "missing", 0); }, "Unknown animation accepted");
        rejects([&] { lm::sprite_frame_at(sprite, "walk", std::numeric_limits<double>::infinity()); }, "Nonfinite animation time accepted");
        auto invalid = sprite_fixture(); invalid["frames"][0]["pixels"][0] = "X.";
        rejects([&] { lm::compile_sprite(source(invalid)); }, "Unknown palette character accepted");
        invalid = sprite_fixture(); invalid["frames"][0]["pixels"][1] = ".";
        rejects([&] { lm::compile_sprite(source(invalid)); }, "Short pixel row accepted");
        invalid = sprite_fixture(); invalid["animations"]["walk"]["durations"][0] = 0;
        rejects([&] { lm::compile_sprite(source(invalid)); }, "Zero frame duration accepted");
        invalid = sprite_fixture(); invalid["animations"]["walk"]["frames"][0] = "absent";
        rejects([&] { lm::compile_sprite(source(invalid)); }, "Missing frame reference accepted");
        invalid = sprite_fixture(); invalid["frames"][1]["id"] = "red";
        rejects([&] { lm::compile_sprite(source(invalid)); }, "Duplicate frame ID accepted");
        invalid = sprite_fixture(); invalid["width"] = 1025;
        rejects([&] { lm::compile_sprite(source(invalid)); }, "Oversize sprite accepted");
        invalid = sprite_fixture(); invalid["palette"]["G"] = "#GGFFFF";
        rejects([&] { lm::compile_sprite(source(invalid)); }, "Invalid hexadecimal color accepted");
        invalid = sprite_fixture(); invalid["surprise"] = 1;
        rejects([&] { lm::compile_sprite(source(invalid)); }, "Unknown sprite field accepted");
        auto drawn = Json{{"schema_version", 1}, {"width", 5}, {"height", 5}, {"frames", Json::array({{{"id", "drawn"}, {"draw", Json::array({
            {{"op", "rect"}, {"x", -1}, {"y", -1}, {"width", 3}, {"height", 3}, {"color", "#FF0000"}},
            {{"op", "circle"}, {"cx", 3}, {"cy", 3}, {"radius", 1}, {"color", "#00FF00"}},
            {{"op", "line"}, {"x1", 0}, {"y1", 4}, {"x2", 4}, {"y2", 0}, {"color", "#0000FF"}}
        })}}})}};
        auto painted = lm::compile_sprite(source(drawn));
        require(painted.default_animation == "default" && painted.frames[0].rgba[0] == 255, "Clipped rectangle/default animation failed");
        require(painted.frames[0].rgba[(3*5+3)*4+1] == 255 && painted.frames[0].rgba[(4*5)*4+2] == 255, "Native circle/line rasterization failed");
        lm::write_sprite_sheet(sprite, output / "fixture.png", 2);
        unsigned w=0, h=0; std::vector<unsigned char> decoded;
        require(lodepng::decode(decoded, w, h, (output/"fixture.png").string()) == 0 && w == 8 && h == 4, "PNG sheet dimensions or decoding failed");
        require(decoded[3] == 128 && decoded[7] == 128 && decoded[4*4+1] == 255, "Nearest-neighbor sheet colors/alpha incorrect");
        std::vector<unsigned char> encoded;
        require(lodepng::encode(encoded, sprite.frames[0].rgba, 2, 2) == 0, "PNG fixture encoding failed");
        auto imported = source({{"schema_version", 1}, {"width", 1}, {"height", 1}, {"source_png", "images/input.png"}, {"frames", Json::array({{{"id", "crop"}, {"source_rect", {0,0,1,1}}}})}});
        imported.inputs["images/input.png"] = std::string(encoded.begin(), encoded.end());
        require(lm::compile_sprite(imported).frames[0].rgba == std::vector<unsigned char>({255,0,0,128}), "PNG input crop changed pixels");
        imported.document["frames"][0]["source_rect"] = {2,2,1,1};
        rejects([&] { lm::compile_sprite(imported); }, "Out-of-bounds PNG crop accepted");
        imported.inputs.clear(); rejects([&] { lm::compile_sprite(imported); }, "Missing PNG input accepted");

        auto cube = lm::compile_mesh(source(mesh_fixture(box()), "mesh"));
        require(cube.vertices.size() == 36 && cube.indices.size() == 36, "Cube triangulation count is incorrect");
        require(cube.minimum == lm::Vec3{-1,-1,-1} && cube.maximum == lm::Vec3{1,1,1}, "Centered cube bounds incorrect");
        require(std::abs(volume(cube) - 8) < 1e-5, "Cube winding/volume incorrect");
        for (const auto& vertex : cube.vertices) require(std::abs(vertex.normal[0]*vertex.normal[0]+vertex.normal[1]*vertex.normal[1]+vertex.normal[2]*vertex.normal[2]-1) < 1e-5, "Mesh normal is not unit length");
        auto other = box(); other["translate"] = {1,0,0};
        for (const auto& [operation, expected] : std::vector<std::pair<std::string,double>>{{"union",12},{"difference",4},{"intersection",4}}) {
            auto result = lm::compile_mesh(source(mesh_fixture({{"op",operation},{"children",Json::array({box(),other})}}),"mesh"));
            require(std::abs(volume(result)-expected) < 1e-4, "CSG "+operation+" volume is incorrect");
        }
        auto transformed = box(1); transformed["scale"]={2,4,6}; transformed["rotate"]={0,0,90}; transformed["translate"]={10,20,30};
        auto moved=lm::compile_mesh(source(mesh_fixture(transformed),"mesh"));
        require(std::abs(moved.minimum[0]-8)<1e-5 && std::abs(moved.maximum[1]-21)<1e-5 && std::abs(moved.maximum[2]-33)<1e-5, "Scale/rotate/translate order incorrect");
        auto cylinder=lm::compile_mesh(source(mesh_fixture({{"op","cylinder"},{"radius",1},{"height",4},{"segments",16}}),"mesh"));
        require(std::abs(cylinder.minimum[1]+2)<1e-5 && std::abs(cylinder.maximum[1]-2)<1e-5 && std::abs(cylinder.maximum[2]-1)<1e-5, "Cylinder is not centered on Y axis");
        invalid=mesh_fixture(box()); invalid["parts"][0]["geometry"]["scale"]={1,-1,1};
        rejects([&] { lm::compile_mesh(source(invalid,"mesh")); }, "Negative scale accepted");
        invalid=mesh_fixture({{"op","sphere"},{"radius",1},{"segments",1000000}});
        rejects([&] { lm::compile_mesh(source(invalid,"mesh")); }, "Unbounded tessellation accepted");
        invalid=mesh_fixture({{"op","difference"},{"children",Json::array({box(),box()})}});
        rejects([&] { lm::compile_mesh(source(invalid,"mesh")); }, "Empty solid accepted");
        Json deep=box(); for(int i=0;i<18;++i) deep={{"op","union"},{"children",Json::array({deep,box()})}};
        rejects([&] { lm::compile_mesh(source(mesh_fixture(deep),"mesh")); }, "Unbounded CSG depth accepted");
        invalid=mesh_fixture(box()); invalid["parts"].push_back(invalid["parts"][0]);
        rejects([&] { lm::compile_mesh(source(invalid,"mesh")); }, "Duplicate named part accepted");
        invalid=mesh_fixture(box()); invalid["parts"][0]["color"]="#FFFFFF80";
        rejects([&] { lm::compile_mesh(source(invalid,"mesh")); }, "Unsupported transparent mesh material accepted");
        invalid=mesh_fixture(box(1000)); invalid["parts"][0]["geometry"]["scale"]={1000,1000,1000};
        rejects([&] { lm::compile_mesh(source(invalid,"mesh")); }, "Unbounded intermediate coordinates accepted");
        auto tiny=box(.001); tiny["scale"]={.001,.001,.001};
        require(lm::compile_mesh(source(mesh_fixture(tiny),"mesh")).vertices.size()==36, "Valid small-scale solid lost its triangles");
        lm::write_mesh_obj(cube,output/"cube.obj");
        std::ifstream obj(output/"cube.obj"); const std::string contents((std::istreambuf_iterator<char>(obj)),std::istreambuf_iterator<char>());
        require(contents.find("g solid\n")!=std::string::npos && contents.find("vn ")!=std::string::npos && contents.find("f ")!=std::string::npos,"OBJ export omitted geometry/normals/named group");
        require(lm::asset_schema("sprite")["properties"].contains("animations") && lm::asset_schema("mesh")["$defs"].contains("geometry"), "Discovery schemas missing main contracts");
        rejects([] { lm::asset_schema("unknown"); }, "Unknown asset schema accepted");
        for(const std::string name: {"rocket","alien","star","planet"}) {
            const auto spr=lm::compile_sprite(recipe(root/"examples/rockets-2d/assets"/(name+".sprite.json"),"sprite"));
            require(!spr.frames.empty(),"Example sprite failed: "+name);
            lm::write_sprite_sheet(spr,output/(name+".png"),name=="planet"?4:8);
            const auto mesh=lm::compile_mesh(recipe(root/"examples/rockets-3d/assets"/(name+".mesh.json"),"mesh"));
            require(!mesh.vertices.empty() && mesh.parts.size()+1==mesh.part_index_offsets.size(),"Example mesh failed: "+name);
            lm::write_mesh_obj(mesh,output/(name+".obj"));
            std::cout<<name<<": "<<spr.frames.size()<<" sprite frames; "<<mesh.vertices.size()<<" mesh vertices\n";
        }
        std::cout<<"Passed "<<checks<<" asset checks. Output: "<<output.string()<<'\n';
        return 0;
    } catch(const std::exception& error) { std::cerr<<"Asset tests failed: "<<error.what()<<'\n'; return 1; }
}
