#include "langmake/project.hpp"
#include "langmake/assets.hpp"
#include "langmake/script_api.hpp"
#include "langmake/cook.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using lm::Json;
int checks = 0;
void expect(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void expect_throw(Function function, const std::string& message) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    expect(threw, message);
}
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    if (!file) throw std::runtime_error("Test fixture write failed.");
}
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
bool ok(const Json& result) { return result.at("ok").get<bool>(); }
Json fixture() {
    return {{"schema_version", 1}, {"dimension", "2d"}, {"title", "Project tests"}, {"script", "scripts/game.luau"},
        {"entities", Json::array({{{"id", "player"}, {"body", "dynamic"}, {"role", "player"}, {"position", {0, 2, 0}}, {"size", {1, 1, 1}}},
            {{"id", "floor"}, {"position", {0, 0, 0}}, {"size", {10, 1, 1}}}})}};
}
Json patch(const std::string& revision, const Json& changes) {
    return {{"expected_revision", revision}, {"changes", changes}};
}
Json change(const std::string& id, const std::string& property, const Json& value) {
    return {{"id", id}, {"property", property}, {"value", value}};
}
Json sprite_fixture() {
    return {{"schema_version", 1}, {"width", 2}, {"height", 2}, {"palette", {{"x", "#ff8844"}, {".", "#00000000"}}},
        {"frames", Json::array({{{"id", "idle"}, {"pixels", {"x.", ".x"}}}})}};
}
void profile_checks(const fs::path& test_root) {
    const auto root=test_root/"profiles";
    auto doc=fixture();doc["schema_version"]=2;doc["project_id"]="profile-fixture";
    doc["save_compatibility"]={{"id","progress-v1"},{"allow_presentation_changes",true}};
    doc["input"]={{"actions",{{"shoot",{{"keys",{"Space"}}}}}}};
    doc["ui"]={{"documents",{{"hud","ui/base.rml"}}},{"expected_characters","Score: 0123456789"}};
    doc["application"]={{"id","org.langmake.profile"},{"icon","art/base.png"},{"platforms",{{"ios",{{"icon","art/ios.png"}}}}}};
    doc["scenarios"]={{"smoke","tests/smoke.json"}};
    doc["profiles"]={{"phone",{{"ui",{{"documents",{{"hud","ui/phone.rml"}}},{"images",{{"button","art/button.png"}}},{"data",{{"layout","phone"}}}}},
        {"input",{{"actions",{{"shoot",{{"keys",{"Left Ctrl"}}}}}},{"touch",{{"buttons",Json::array({{{"action","shoot"},{"label","Fire"}}})}}}}},
        {"camera",{{"position",{0,5,20}},{"target",{0,2,0}}}},
        {"application",{{"id","org.langmake.phone"},{"label","Phone Game"},{"icon","art/phone.png"},{"orientation","landscape"}}}}},
        {"tablet",{{"ui",{{"documents",{{"hud","ui/tablet.rml"}}}}},{"application",{{"icon","art/tablet.png"}}}}}};
    write(root/"project.json",doc.dump(2));write(root/"scripts/game.luau","return {}\n");
    for(auto file:{"base.rml","phone.rml","tablet.rml"})write(root/"ui"/file,"<rml><body>HUD</body></rml>");
    for(auto file:{"base.png","ios.png","button.png","phone.png","tablet.png"})write(root/"art"/file,"icon fixture");
    write(root/"tests/smoke.json","{\"steps\":1,\"input\":\"tests/input.json\"}");
    write(root/"tests/input.json","{\"events\":[]}");
    auto base=lm::load_project(root),phone=lm::load_project(root,"phone");
    expect(base.profile.empty()&&phone.profile=="phone","Profile was inferred or explicit selection ignored.");
    expect(base.source_revision==phone.source_revision&&base.revision!=phone.revision,"Source graph and selected revision must be distinct concepts.");
    expect(phone.entities[0].position==base.entities[0].position&&phone.entities[0].size==base.entities[0].size,"Presentation profile modified gameplay geometry.");
    expect(phone.ui["data"]["layout"]=="phone"&&!phone.ui.contains("expected_characters"),"Profiles must replace whole fields, not recursively merge UI.");
    expect(phone.application["id"]=="org.langmake.phone"&&phone.application["version"]=="1.0"&&phone.application["build"]==1,"Application selection/defaults differ.");
    expect(phone.sources.contains("ui/tablet.rml")&&phone.sources.contains("art/ios.png")&&phone.sources.contains("art/tablet.png")&&phone.sources.contains("tests/input.json"),"Inactive profile, application or scenario dependencies were omitted.");
    expect(phone.scenarios["smoke"]["steps"]==1&&lm::project_summary(phone)["profile"]=="phone","Resolved scenario/profile evidence missing.");
    expect(lm::save_structure_fingerprint(base)==lm::save_structure_fingerprint(phone),"Presentation overrides changed compatible structure.");
    write(root/"ui/tablet.rml","<rml><body>Changed inactive profile</body></rml>");
    auto changed=lm::load_project(root,"phone");
    expect(changed.revision!=phone.revision&&lm::save_structure_fingerprint(changed)==lm::save_structure_fingerprint(phone),"Inactive UI revision or gameplay fingerprint tracking is wrong.");
    phone=changed;
    lm::copy_project_sources(phone,test_root/"profile-copy");
    expect(lm::load_project(test_root/"profile-copy","phone").revision==phone.revision,"Copy omitted inactive presentation sources.");
    auto bad=doc;bad["profiles"]["phone"]["entities"]=Json::array();
    expect(!ok(lm::validate_document(bad)),"Profile gameplay override accepted.");
    bad=doc;bad["profiles"]["phone"]["application"]["signing_key"]="secret";
    expect(!ok(lm::validate_document(bad)),"Machine/signing metadata accepted in portable application.");
    bad=doc;bad["application"]["icon"]="../escape.png";
    expect(!ok(lm::validate_document(bad)),"Application icon traversal accepted.");
    bad=doc;bad["default_profile"]="missing";
    expect(!ok(lm::validate_document(bad)),"Unregistered default profile accepted.");
    expect_throw([&]{lm::load_project(root,"missing");},"Unknown explicit profile accepted.");
    bad=doc;bad["ui"]["expected_characters"]=std::string(16385,'x');
    expect(!ok(lm::validate_document(bad)),"Unbounded glyph coverage string accepted.");
    write(root/"tests/smoke.json","{\"input\":\"../escape.json\"}");
    expect_throw([&]{lm::load_project(root);},"Scenario input traversal accepted.");
    write(root/"tests/smoke.json","{\"steps\":1,\"input\":\"tests/input.json\"}");
    expect(ok(lm::checkpoint_project(root,"profiles")),"Profile graph checkpoint failed.");
    write(root/"art/tablet.png","changed");fs::remove(root/"tests/input.json");
    expect(ok(lm::restore_project(root,"profiles"))&&lm::load_project(root,"phone").revision==phone.revision,"Checkpoint omitted inactive profile/icon/scenario files.");
    const auto cooked=lm::cook_project(root,test_root/"cooked-profile","android","phone");
    const auto packaged=lm::load_project(test_root/"cooked-profile");
    expect(cooked["profile"]=="phone"&&packaged.profile=="phone"&&packaged.ui==phone.ui,"Cook did not resolve selected presentation for default player loading.");
    expect(packaged.sources.contains("ui/tablet.rml")&&packaged.sources.contains("tests/input.json"),"Cook dropped inactive profile/scenario dependencies.");
    expect(lm::save_structure_fingerprint(packaged)==lm::save_structure_fingerprint(phone),"Cooking presentation altered gameplay save structure.");
    const auto inventory=Json::parse(read(test_root/"cooked-profile/cook-manifest.json"));
    expect(inventory["application"]["id"]=="org.langmake.phone"&&inventory["resolved_profile"]==phone.resolved_profile,"Cook evidence omitted resolved application/profile.");
    doc["default_profile"]="phone";write(root/"project.json",doc.dump());
    expect(lm::load_project(root).profile=="phone"&&lm::load_project(root,"tablet").profile=="tablet","Explicit selection must override authored default profile.");
    expect(ok(lm::checkpoint_project(root,"default-profile"))&&ok(lm::restore_project(root,"default-profile")),"Default-profile source checkpoint revision failed.");
    doc["scenes"]={{"room","scenes/room.json"}};doc["entry_scene"]="room";
    write(root/"scenes/room.json",Json({{"ui",{{"documents",{{"hud","ui/room.rml"}}}}}}).dump());
    write(root/"ui/room.rml","<rml><body>Scene UI</body></rml>");write(root/"project.json",doc.dump());
    const auto selected_scene=lm::load_project(root);
    expect(selected_scene.scene_id=="room"&&selected_scene.ui["documents"]["hud"]=="ui/phone.rml"&&selected_scene.sources.contains("ui/room.rml"),"Profile must apply after scene overrides while preserving inactive scene UI sources.");
}
void asset_checks(const fs::path& test_root, const std::string& script) {
    const auto root = test_root / "asset-project";
    const auto manifest = root / "project.json";
    auto document = fixture();
    document["assets"] = {{"courier", {{"type", "sprite"}, {"path", "assets/courier.json"}}},
        {"imported", {{"type", "sprite"}, {"path", "assets/imported.json"}}},
        {"scenery", {{"type", "mesh"}, {"path", "assets/scenery.json"}}},
        {"theme", {{"type", "music"}, {"path", "assets/theme.json"}}}};
    document["music"] = "theme";
    document["entities"][0]["asset"] = "courier";
    document["entities"][0]["animation"] = "default";
    document["entities"][0]["rotation"] = {0, 0, 30};
    document["entities"][0]["flip_x"] = true;
    document["entities"][0]["animation_speed"] = 2;
    document["entities"][1]["asset"] = "scenery";
    const auto sprite = sprite_fixture();
    const Json imported{{"schema_version", 1}, {"width", 2}, {"height", 2}, {"source_png", "art/source.png"},
        {"frames", Json::array({{{"id", "crop"}, {"source_rect", {0, 0, 2, 2}}}})}};
    const Json mesh{{"schema_version", 1}, {"parts", Json::array({{{"id", "cube"}, {"color", "#8899aa"},
        {"geometry", {{"op", "box"}, {"size", {1, 1, 1}}}}}})}};
    const Json music{{"schema_version", 1}, {"type", "music"}, {"bpm", 120}, {"ppq", 96}, {"length", 96},
        {"instruments", {{"lead", {{"wave", "sine"}}}}},
        {"notes", Json::array({{{"tick", 0}, {"duration", 48}, {"note", 60}, {"instrument", "lead"}}})}};
    write(manifest, document.dump(2) + "\n");
    write(root / "scripts/game.luau", script);
    write(root / "assets/courier.json", sprite.dump(2) + "\n");
    write(root / "assets/imported.json", imported.dump(2) + "\n");
    write(root / "assets/scenery.json", mesh.dump(2) + "\n");
    write(root / "assets/theme.json", music.dump(2) + "\n");
    lm::AssetSource pixels;
    pixels.type = "sprite";
    pixels.document = sprite;
    lm::write_sprite_sheet(lm::compile_sprite(pixels), root / "art/source.png");

    const auto loaded = lm::load_project(root);
    expect(loaded.assets.size() == 4 && loaded.music == "theme", "Registered assets or background music were not loaded.");
    expect(loaded.assets.at("courier").path == fs::weakly_canonical(root / "assets/courier.json"), "Asset recipe path was not resolved.");
    expect(loaded.assets.at("imported").inputs.at("art/source.png") == read(root / "art/source.png"), "External PNG input bytes not preserved.");
    expect(loaded.entities[0].color == lm::Color{1,1,1,1}, "Asset visual default tint should be white.");
    expect(loaded.entities[0].rotation[2] == 30 && loaded.entities[0].flip_x && loaded.entities[0].animation_speed == 2, "Visual fields not loaded.");
    expect(loaded.entities[0].animation == "default", "Synthetic default animation rejected.");
    expect(lm::project_summary(loaded)["assets"]["imported"]["inputs"][0]["path"] == "art/source.png", "Inspect omitted PNG dependency metadata.");
    expect(lm::capabilities()["limits"]["max_assets"] == 256 && lm::project_schema()["properties"]["assets"]["maxProperties"] == 256, "Asset discovery limits differ.");

    auto invalid = document;
    invalid["assets"]["courier"]["path"] = "../outside.json";
    expect(!ok(lm::validate_document(invalid)), "Asset path traversal accepted.");
    invalid = document;
    invalid["assets"]["courier"]["extra"] = 1;
    expect(!ok(lm::validate_document(invalid)), "Unknown asset registration field accepted.");
    invalid = document;
    invalid["assets"]["courier"]["type"] = "texture";
    expect(!ok(lm::validate_document(invalid)), "Unknown asset type accepted.");
    invalid = document;
    invalid["assets"]["bad/id"] = invalid["assets"]["courier"];
    expect(!ok(lm::validate_document(invalid)), "Malformed asset ID accepted.");
    invalid = document;
    invalid["assets"]["scenery"]["path"] = "ASSETS/COURIER.json";
    expect(!ok(lm::validate_document(invalid)), "Case-insensitive duplicate recipe path accepted.");
    invalid = document;
    invalid["music"] = "courier";
    expect(!ok(lm::validate_document(invalid)), "Sprite selected as background music.");
    for (const auto& entry : std::vector<std::pair<std::string, Json>>{{"asset", "missing"}, {"asset", "theme"},
        {"animation", 3}, {"animation_speed", 0}, {"rotation", {0, 0, 360001}}, {"flip_x", 1}}) {
        invalid = document;
        invalid["entities"][0][entry.first] = entry.second;
        expect(!ok(lm::validate_document(invalid)), "Invalid visual property accepted: " + entry.first);
    }
    invalid = document;
    invalid["entities"][1]["animation"] = "idle";
    expect(!ok(lm::validate_document(invalid)), "Mesh animation reference accepted.");
    invalid = document;
    invalid["entities"][1]["color"] = {1, 1, 1, 0.5};
    expect(!ok(lm::validate_document(invalid)), "Translucent mesh entity tint accepted.");
    invalid["entities"][1]["asset"] = "courier";
    expect(ok(lm::validate_document(invalid)), "Translucent sprite entity tint rejected.");
    invalid = fixture();
    invalid["assets"] = Json::object();
    for (int i = 0; i < 256; ++i) invalid["assets"]["sprite_" + std::to_string(i)] = {{"type", "sprite"}, {"path", "assets/" + std::to_string(i) + ".json"}};
    expect(ok(lm::validate_document(invalid)), "256-asset boundary rejected.");
    invalid["assets"]["overflow"] = {{"type", "sprite"}, {"path", "assets/overflow.json"}};
    expect(!ok(lm::validate_document(invalid)), "Asset count over limit accepted.");

    const auto original_sprite = read(root / "assets/courier.json");
    write(root / "assets/courier.json", original_sprite + " \n");
    const auto recipe_changed = lm::load_project(root);
    expect(recipe_changed.revision != loaded.revision && recipe_changed.assets.at("courier").revision != loaded.assets.at("courier").revision, "Recipe-only edit did not change revisions.");
    expect(recipe_changed.assets.at("theme").revision == loaded.assets.at("theme").revision, "Unchanged asset revision changed.");
    expect(!ok(lm::patch_project(root, patch(loaded.revision, Json::array({change("player", "visible", false)})))), "Patch ignored changed asset revision.");
    write(root / "assets/courier.json", original_sprite);
    const auto original_png = read(root / "art/source.png");
    write(root / "art/source.png", original_png + "external bytes");
    const auto png_changed = lm::load_project(root);
    expect(png_changed.revision != loaded.revision && png_changed.assets.at("imported").revision != loaded.assets.at("imported").revision, "PNG-only edit did not change revisions.");
    write(root / "art/source.png", original_png);
    auto alternate_input = imported;
    alternate_input["source_png"] = "art/other.png";
    write(root / "art/other.png", original_png);
    write(root / "assets/imported.json", alternate_input.dump());
    expect(lm::load_project(root).assets.at("imported").revision != loaded.assets.at("imported").revision, "Input source path did not change asset revision.");
    write(root / "assets/imported.json", imported.dump(2) + "\n");

    invalid = document;
    invalid["entities"][0]["animation"] = "missing";
    write(manifest, invalid.dump());
    expect_throw([&] { lm::load_project(root); }, "Unknown named sprite animation accepted.");
    write(manifest, document.dump(2) + "\n");
    write(root / "assets/courier.json", "{\"schema_version\":1,\"nested\":{\"x\":1,\"x\":2}}");
    expect_throw([&] { lm::load_project(root); }, "Nested duplicate asset keys accepted.");
    write(root / "assets/courier.json", original_sprite);
    auto unsafe_input = imported;
    unsafe_input["source_png"] = "../outside.png";
    write(root / "assets/imported.json", unsafe_input.dump());
    expect_throw([&] { lm::load_project(root); }, "PNG traversal accepted.");
    unsafe_input["source_png"] = 4;
    write(root / "assets/imported.json", unsafe_input.dump());
    expect_throw([&] { lm::load_project(root); }, "Nonstrings accepted as PNG source paths.");
    write(root / "assets/imported.json", imported.dump(2) + "\n");
    const auto visual_patch = lm::patch_project(root, patch(loaded.revision, Json::array({change("player", "rotation", {10, 20, 40}),
        change("player", "flip_x", false), change("player", "animation_speed", 0.5), change("player", "asset", "imported")})));
    expect(ok(visual_patch), "Valid visual patch failed: " + visual_patch.dump());
    const auto visual = lm::load_project(root);
    expect(visual.entities[0].asset == "imported" && visual.entities[0].rotation[1] == 20 && !visual.entities[0].flip_x && visual.entities[0].animation_speed == 0.5, "Visual patch not persisted.");
    expect(visual_patch["revision"] == visual.revision, "Visual patch returned wrong full source revision.");
    const auto before_bad_patch = read(manifest);
    expect(!ok(lm::patch_project(root, patch(visual.revision, Json::array({change("player", "animation", "missing")})))) && read(manifest) == before_bad_patch, "Invalid animation patch changed manifest.");

    auto bad_sprite = sprite;
    bad_sprite["frames"][0]["pixels"][0] = "unknown";
    expect(!ok(lm::replace_asset(root, "courier", visual.revision, bad_sprite)) && read(root / "assets/courier.json") == original_sprite, "Invalid sprite compilation changed recipe.");
    auto next_sprite = sprite;
    next_sprite["palette"]["x"] = "#4488ff";
    expect(!ok(lm::replace_asset(root, "courier", "old-revision", next_sprite)), "Stale asset replacement accepted.");
    expect(!ok(lm::replace_asset(root, "missing", visual.revision, next_sprite)), "Unknown replacement asset accepted.");
    const auto replaced = lm::replace_asset(root, "courier", visual.revision, next_sprite);
    expect(ok(replaced), "Valid sprite replacement failed: " + replaced.dump());
    expect(replaced["revision"] == lm::load_project(root).revision && replaced["asset_revision"] == lm::load_project(root).assets.at("courier").revision, "Asset replacement revisions are wrong.");
    expect(read(root / ".langmake/assets-before-patch/courier.json") == original_sprite, "Asset replacement backup lost original bytes.");
    const auto mesh_replaced = lm::replace_asset(root, "scenery", lm::load_project(root).revision, mesh);
    expect(ok(mesh_replaced), "Mesh replacement compilation failed: " + mesh_replaced.dump());
    const auto music_replaced = lm::replace_asset(root, "theme", lm::load_project(root).revision, music);
    expect(ok(music_replaced), "Music replacement compilation failed: " + music_replaced.dump());
    expect(ok(lm::replace_asset(root, "imported", lm::load_project(root).revision, imported)), "Imported sprite replacement compilation failed.");
    auto bad_mesh = mesh;
    bad_mesh["parts"][0]["geometry"]["size"] = {0, 1, 1};
    const auto before_invalid_graph = read(root / "assets/courier.json");
    const auto valid_mesh_bytes = read(root / "assets/scenery.json");
    write(root / "assets/scenery.json", bad_mesh.dump());
    expect(!ok(lm::replace_asset(root, "courier", lm::load_project(root).revision, sprite)) &&
        read(root / "assets/courier.json") == before_invalid_graph, "Replacement ignored a failed asset in the candidate project graph.");
    // Replacing the invalid source itself can repair a parseable broken graph.
    expect(ok(lm::replace_asset(root, "scenery", lm::load_project(root).revision, mesh)), "Replacement could not repair a semantically invalid asset.");
    expect(read(root / "assets/scenery.json") == valid_mesh_bytes, "Asset repair changed expected recipe serialization.");
    expect(!fs::exists(root / ".langmake/mutation.lock"), "Mutation directory remains after release.");
    fs::create_directory(root / ".langmake/mutation.lock");
    expect(!ok(lm::replace_asset(root, "courier", lm::load_project(root).revision, sprite)), "Asset replacement bypassed another mutation lock.");
    fs::remove(root / ".langmake/mutation.lock");

    const auto source = lm::load_project(root);
    const auto copied_root = test_root / "copied-assets";
    lm::copy_project_sources(source, copied_root);
    const auto copied = lm::load_project(copied_root);
    expect(copied.revision == source.revision && copied.assets.at("imported").inputs == source.assets.at("imported").inputs, "Project copy omitted source dependencies.");
    expect(!fs::exists(copied_root / ".langmake"), "Project copy included generated state.");
    expect_throw([&] { lm::copy_project_sources(source, copied_root); }, "Project copy overwrote destination.");
    write(root / "assets/courier.json", read(root / "assets/courier.json") + "\n");
    expect_throw([&] { lm::copy_project_sources(source, test_root / "stale-copy"); }, "Project copy ignored stale source revision.");
    expect(!fs::exists(test_root / "stale-copy"), "Failed project copy left a destination.");
    write(root / "assets/courier.json", source.assets.at("courier").bytes);

    const auto saved = lm::checkpoint_project(root, "with-assets");
    expect(ok(saved) && saved["source_files"] == 7, "Asset checkpoint did not save every source: " + saved.dump());
    const auto saved_manifest = read(manifest), saved_script = read(root / "scripts/game.luau");
    fs::remove(root / "assets/courier.json");
    fs::remove(root / "art/source.png");
    write(root / "assets/theme.json", "broken asset");
    write(manifest, "broken manifest");
    write(root / "scripts/game.luau", "changed script");
    const auto restored = lm::restore_project(root, "with-assets");
    expect(ok(restored), "Restore did not recover malformed or missing asset inputs: " + restored.dump());
    expect(read(manifest) == saved_manifest && read(root / "scripts/game.luau") == saved_script && read(root / "art/source.png") == original_png, "Asset restore did not preserve exact bytes.");
    expect(lm::load_project(root).revision == source.revision, "Restored asset source graph has wrong revision.");
    const auto snapshot_png = root / ".langmake/checkpoints/with-assets/files/art/source.png";
    write(snapshot_png, original_png + "tamper");
    expect(!ok(lm::restore_project(root, "with-assets")) && lm::load_project(root).revision == source.revision, "Tampered asset checkpoint changed live sources.");
    write(snapshot_png, original_png);
    const auto metadata_path = root / ".langmake/checkpoints/with-assets/checkpoint.json";
    const auto saved_metadata = read(metadata_path);
    auto metadata = Json::parse(saved_metadata);
    metadata["files"]["../escape.png"] = "fake";
    write(metadata_path, metadata.dump());
    expect(!ok(lm::restore_project(root, "with-assets")), "Checkpoint with unreferenced traversal source accepted.");
    write(metadata_path, saved_metadata);

#ifdef _WIN32
    // A shared-read-only handle allows preflight reads but makes replacement fail
    // after preceding asset writes, exercising rollback without fault injection.
    write(root / "assets/courier.json", source.assets.at("courier").bytes + "\n");
    write(root / "assets/theme.json", source.assets.at("theme").bytes + "\n");
    const auto before_rollback = lm::load_project(root).revision;
    const HANDLE held = CreateFileW((root / "assets/theme.json").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    expect(held != INVALID_HANDLE_VALUE, "Could not create rollback test file handle.");
    const auto failed_restore = lm::restore_project(root, "with-assets");
    CloseHandle(held);
    expect(!ok(failed_restore), "Restore unexpectedly replaced a locked file.");
    expect(lm::load_project(root).revision == before_rollback, "Failed multi-file restore did not roll back earlier asset replacements.");
    expect(ok(lm::restore_project(root, "with-assets")), "Restore failed after replacement lock was released.");
#endif
    const auto large_root = test_root / "source-limits";
    auto large = fixture();
    large["assets"] = Json::object();
    write(large_root / "scripts/game.luau", script);
    const std::string large_recipe = "{}" + std::string(4 * 1024 * 1024 - 2, ' ');
    for (int i = 0; i < 8; ++i) {
        const auto path = "assets/large" + std::to_string(i) + ".json";
        large["assets"]["large" + std::to_string(i)] = {{"type", "sprite"}, {"path", path}};
        write(large_root / path, large_recipe);
    }
    write(large_root / "project.json", large.dump());
    expect_throw([&] { lm::load_project(large_root); }, "Source graph above 32 MiB accepted.");
    large["assets"].erase("large7");
    write(large_root / "project.json", large.dump());
    expect(lm::load_project(large_root).assets.size() == 7, "Sources under total limit with exact 4 MiB files rejected.");
    write(large_root / "assets/large0.json", large_recipe + " ");
    expect_throw([&] { lm::load_project(large_root); }, "Single asset over 4 MiB accepted.");
}
}

int main(int argc, char** argv) {
    // CMake passes the source root. All test writes stay below that workspace.
    const auto root = fs::weakly_canonical(argc > 1 ? fs::path(argv[1]) : fs::path(__FILE__).parent_path().parent_path());
    const auto test_root = root / ".langmake" / ("project-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto manifest = test_root / "project.json";
        const auto script_file = test_root / "scripts/game.luau";
        const std::string script = "return { update = function(dt) end }\n";
        const auto document = fixture();
        {
            auto touch=fixture();
            touch["input"]={{"actions",{{"interact",{{"keys",{"E"}}}}}},
                {"touch",{{"stick",{{"x_action","move_x"},{"y_action","move_z"}}},
                    {"buttons",Json::array({{{"action","interact"},{"label","Talk"}},{{"action","jump"}}})}}}};
            expect(ok(lm::validate_document(touch)),"Valid mobile action configuration was rejected");
            auto invalid=touch;invalid["input"]["touch"]["buttons"][0]["action"]="missing";
            expect(!ok(lm::validate_document(invalid)),"Unregistered touch action was accepted");
            invalid=touch;invalid["input"]["touch"]["buttons"].push_back({{"action","interact"}});
            expect(!ok(lm::validate_document(invalid)),"Duplicate touch actions were accepted");
            invalid=touch;invalid["input"]["touch"]["stick"]["x_action"]=42;
            expect(!ok(lm::validate_document(invalid)),"Invalid joystick axis was accepted");
            invalid=touch;invalid["input"]["touch"]["enabled"]="no";
            expect(!ok(lm::validate_document(invalid)),"Invalid touch enabled flag was accepted");
            invalid=touch;invalid["input"]["touch"]["buttons"][0]["label"]=std::string(25,'x');
            expect(!ok(lm::validate_document(invalid)),"Oversized touch label was accepted");
        }
        write(manifest, document.dump(2) + "\n");
        write(script_file, script);

        expect(ok(lm::validate_document(document)), "Valid document rejected.");
        auto boundary = document;
        boundary["entities"] = Json::array();
        for (int i = 0; i < 4096; ++i) {
            auto item = document["entities"][1];
            item["id"] = "entity_" + std::to_string(i);
            boundary["entities"].push_back(std::move(item));
        }
        expect(ok(lm::validate_document(boundary)), "4096-entity world boundary rejected.");
        auto extra = document["entities"][1];
        extra["id"] = "entity_4096";
        boundary["entities"].push_back(extra);
        expect(!ok(lm::validate_document(boundary)), "More than the 4096 world entity cap accepted.");
        expect(lm::project_schema()["properties"]["entities"]["maxItems"] == 4096, "Schema and world entity caps differ.");
        expect(lm::capabilities()["limits"]["max_entities"] == 4096, "Capabilities and world entity caps differ.");
        auto malformed = document;
        malformed["surprise"] = true;
        expect(!ok(lm::validate_document(malformed)), "Unknown project field accepted.");
        malformed = document;
        malformed["entities"][0]["surprise"] = 0;
        expect(!ok(lm::validate_document(malformed)), "Unknown entity field accepted.");
        malformed = document;
        malformed["camera"] = {{"position", {0, 0, 0}}, {"target", {0, 0, 0}}};
        expect(!ok(lm::validate_document(malformed)), "Degenerate camera accepted.");
        malformed = document;
        malformed["entities"][1]["id"] = "player";
        expect(!ok(lm::validate_document(malformed)), "Duplicate entity ID accepted.");
        malformed = document;
        malformed["entities"][0]["size"] = {0, 1, 1};
        expect(!ok(lm::validate_document(malformed)), "Zero-size entity accepted.");
        malformed = document;
        malformed["entities"][1]["shape"] = "sphere";
        malformed["entities"][1]["size"] = {2, 1, 2};
        expect(!ok(lm::validate_document(malformed)), "Nonuniform collidable sphere accepted.");
        malformed["entities"][1]["body"] = "none";
        expect(ok(lm::validate_document(malformed)), "Stretched noncollidable sphere visual rejected.");
        malformed["entities"][1]["body"] = "static";
        malformed["entities"][1]["size"] = {2, 2, 2};
        expect(ok(lm::validate_document(malformed)), "Uniform collidable sphere rejected.");
        malformed = document;
        malformed["entities"][0]["mass"] = std::numeric_limits<double>::infinity();
        expect(!ok(lm::validate_document(malformed)), "Infinite mass accepted.");
        malformed = document;
        malformed["entities"][0]["position"] = {"no", 0, 0};
        expect(!ok(lm::validate_document(malformed)), "Nonnumeric position accepted.");
        malformed = document;
        malformed["entities"][0]["body"] = "static";
        expect(!ok(lm::validate_document(malformed)), "Static player accepted.");
        malformed = document;
        malformed["entities"][1]["body"] = "dynamic";
        malformed["entities"][1]["role"] = "player";
        expect(!ok(lm::validate_document(malformed)), "Multiple players accepted.");
        for (const auto* unsafe : {"../escape.luau", "/absolute.luau", "C:/escape.luau", "scripts\\escape.luau", ".langmake/hidden.luau", "scripts/../escape.luau"}) {
            malformed = document;
            malformed["script"] = unsafe;
            expect(!ok(lm::validate_document(malformed)), std::string("Unsafe script path accepted: ") + unsafe);
        }

        const auto loaded = lm::load_project(test_root);
        expect(loaded.entities.size() == 2, "Wrong entity count.");
        expect(loaded.entities[1].name == "floor" && loaded.entities[1].body == "static", "Entity defaults not applied.");
        expect(loaded.script_source == script, "Wrong script source.");
        expect(ok(lm::project_summary(loaded)), "Inspect failed.");
        expect(lm::project_summary(loaded)["entities"][0]["id"] == "player", "Inspect omitted stable entity ID.");
        expect(lm::project_schema().is_object() && ok(lm::capabilities()), "Discovery APIs failed.");
        const auto discovery = lm::capabilities();
        expect(discovery["starter_templates"] == Json::array({"2d","3d","rockets-2d","rockets-3d","adventure","strategy","shared-2d","workflow-2d"}),
            "Capabilities must advertise all eight supported starters.");
        for (const auto& starter : discovery["starter_templates"])
            expect(fs::is_regular_file(root / "examples" / starter.get<std::string>() / "project.json"), "Discovered starter source is missing.");
        expect(discovery["ios"]["qualification"] == "docs/27-workflow-qualification.md" &&
            fs::is_regular_file(root / discovery["ios"]["qualification"].get<std::string>()), "iOS discovery must refer to current qualification evidence.");

        const auto original_bytes = read(manifest);
        const auto stale = lm::patch_project(test_root, patch("stale", Json::array({change("player", "position", {1, 2, 0})})));
        expect(!ok(stale) && stale["current_revision"] == loaded.revision, "Stale patch not rejected with current revision.");
        expect(read(manifest) == original_bytes, "Stale patch changed disk content.");
        const auto partial = lm::patch_project(test_root, patch(loaded.revision, Json::array({change("player", "position", {1, 2, 0}), change("floor", "size", {-1, 1, 1})})));
        expect(!ok(partial) && read(manifest) == original_bytes, "Invalid batch partially applied.");
        expect(!ok(lm::patch_project(test_root, patch(loaded.revision, Json::array({change("missing", "visible", false)})))), "Missing entity accepted.");
        expect(!ok(lm::patch_project(test_root, patch(loaded.revision, Json::array({change("player", "id", "renamed")})))), "Immutable ID patch accepted.");
        expect(!ok(lm::patch_project(test_root, patch(loaded.revision, Json::array({change("player", "visible", false), change("player", "visible", true)})))), "Duplicate property patch accepted.");
        const auto applied = lm::patch_project(test_root, patch(loaded.revision, Json::array({change("player", "position", {3, 4, 0}), change("player", "name", "Updated courier")})));
        expect(ok(applied), "Valid patch failed: " + applied.dump());
        const auto updated = lm::load_project(test_root);
        expect(updated.entities[0].position[0] == 3 && updated.entities[0].name == "Updated courier", "Valid patch not persisted.");
        expect(updated.revision == applied["revision"].get<std::string>() && updated.revision != loaded.revision, "Wrong post-patch revision.");
        expect(read(test_root / ".langmake/project.before-patch.json") == original_bytes, "Patch backup did not preserve old bytes.");
        write(script_file, script + "-- external script edit\n");
        const auto script_changed = lm::load_project(test_root);
        expect(script_changed.revision != updated.revision, "Script changes did not affect revision.");
        expect(!ok(lm::patch_project(test_root, patch(updated.revision, Json::array({change("player", "visible", false)})))), "Patch ignored changed script revision.");

        const auto checkpoint = lm::checkpoint_project(test_root, "before-edit");
        expect(ok(checkpoint), "Checkpoint failed: " + checkpoint.dump());
        expect(!ok(lm::checkpoint_project(test_root, "before-edit")), "Existing checkpoint overwritten.");
        expect(!ok(lm::checkpoint_project(test_root, "../escape")), "Checkpoint traversal accepted.");
        expect(!ok(lm::restore_project(test_root, "../escape")), "Restore traversal accepted.");
        const auto checkpoint_manifest = read(manifest), checkpoint_script = read(script_file);
        const auto changed_again = lm::patch_project(test_root, patch(script_changed.revision, Json::array({change("player", "position", {9, 4, 0})})));
        expect(ok(changed_again), "Second patch failed.");
        write(script_file, "return {}\n");
        const auto restored = lm::restore_project(test_root, "before-edit");
        expect(ok(restored), "Restore failed: " + restored.dump());
        expect(read(manifest) == checkpoint_manifest && read(script_file) == checkpoint_script, "Restore did not recover both exact files.");
        expect(lm::load_project(test_root).revision == script_changed.revision, "Restore revision does not match checkpoint.");
        write(manifest, "{ broken manifest");
        expect_throw([&] { lm::load_project(test_root); }, "Malformed JSON accepted.");
        expect(ok(lm::restore_project(test_root, "before-edit")), "Restore cannot recover a malformed live manifest.");
        fs::remove(script_file);
        expect(ok(lm::restore_project(test_root, "before-edit")), "Restore cannot recover a missing live script.");

        write(manifest, "{\"schema_version\":1,\"schema_version\":1}");
        expect_throw([&] { lm::load_project(test_root); }, "Duplicate JSON keys silently accepted.");
        expect(ok(lm::restore_project(test_root, "before-edit")), "Could not restore after duplicate-key test.");
        const auto saved_checkpoint = read(test_root / ".langmake/checkpoints/before-edit/game.luau");
        write(test_root / ".langmake/checkpoints/before-edit/game.luau", "tampered\n");
        const auto before_tampered_restore = read(manifest);
        expect(!ok(lm::restore_project(test_root, "before-edit")), "Mismatched checkpoint content accepted.");
        expect(read(manifest) == before_tampered_restore, "Failed restore changed manifest.");
        write(test_root / ".langmake/checkpoints/before-edit/game.luau", saved_checkpoint);

        // Version 0.1 checkpoints used only the manifest/script pair and a
        // different project revision. They remain restorable after migration.
        const auto legacy_path = test_root / ".langmake/checkpoints/legacy";
        write(legacy_path / "project.json", checkpoint_manifest);
        write(legacy_path / "game.luau", checkpoint_script);
        const auto legacy_revision = lm::revision_for(std::to_string(checkpoint_manifest.size()) + ":" + checkpoint_manifest +
            std::to_string(checkpoint_script.size()) + ":" + checkpoint_script);
        write(legacy_path / "checkpoint.json", Json({{"format_version", 1}, {"script", "scripts/game.luau"},
            {"revision", legacy_revision}, {"manifest_hash", lm::revision_for(checkpoint_manifest)},
            {"script_hash", lm::revision_for(checkpoint_script)}}).dump());
        write(script_file, "legacy recovery test");
        const auto legacy_restored = lm::restore_project(test_root, "legacy");
        expect(ok(legacy_restored), "Version 0.1 checkpoint no longer restores: " + legacy_restored.dump());
        expect(lm::load_project(test_root).revision == script_changed.revision && legacy_restored["revision"] == script_changed.revision,
            "Legacy restore did not migrate its content to the current revision scheme.");
        write(legacy_path / "game.luau", "tampered legacy");
        expect(!ok(lm::restore_project(test_root, "legacy")), "Legacy checkpoint hash tampering accepted.");

        // A sibling folder is outside this project root but still inside the authorized workspace.
        const auto sibling = test_root / "outside-project";
        const auto nested_project = test_root / "nested-project";
        write(sibling / "outside.luau", "return {}\n");
        write(nested_project / "project.json", document.dump());
        std::error_code symlink_error;
        fs::create_directory_symlink(sibling, nested_project / "scripts", symlink_error);
        if (!symlink_error) {
            auto escaped = document;
            escaped["script"] = "scripts/outside.luau";
            write(nested_project / "project.json", escaped.dump());
            expect_throw([&] { lm::load_project(nested_project); }, "Symlink path escaped the project root.");
        } else std::cout << "SKIP: directory-symlink confinement test (host disallows symlink creation).\n";

        for (const auto* example : {"2d", "3d"}) {
            const auto sample = lm::load_project(root / "examples" / example);
            expect(sample.dimension == example && sample.entities.size() > 20, "Example did not load correctly.");
        }
        asset_checks(test_root, script);
        profile_checks(test_root);
        {
            const auto modern_root=test_root/"modern";
            auto modern=fixture();modern["schema_version"]=2;modern["project_id"]="source-graph-test";
            modern["modules"]={{"math","modules/math.luau"}};
            modern["scenes"]={{"room","scenes/room.json"}};modern["entry_scene"]="room";
            modern["ui"]={{"documents",{{"hud","ui/hud.rml"}}},{"styles",{{"style","ui/style.rcss"}}},{"fonts",{{"font","ui/font.bin"}}}};
            modern["definitions"]={{"items","data/items.json"}};
            modern["prefabs"]={{"crate","prefabs/crate.json"}};
            write(modern_root/"project.json",modern.dump());write(modern_root/"scripts/game.luau","return {}\n");
            write(modern_root/"scripts/room.luau","local math = require('math')\nreturn {start = function() engine.state_set('count',math.value) end}\n");
            write(modern_root/"modules/math.luau","return {value=42}\n");
            write(modern_root/"scenes/room.json",Json({{"script","scripts/room.luau"},{"entities",modern["entities"]}}).dump());
            write(modern_root/"ui/hud.rml","<rml><head></head><body>Hello</body></rml>");write(modern_root/"ui/style.rcss","body { color: white; }");
            const std::string binary(5*1024*1024,'x');write(modern_root/"ui/font.bin",binary);
            write(modern_root/"data/items.json","{\"key\":{\"name\":\"Key\"}}");
            write(modern_root/"prefabs/crate.json",Json({{"id","crate"},{"position",{0,0,0}},{"size",{1,1,1}},{"body","none"}}).dump());
            auto loaded=lm::load_project(modern_root);const auto initial_revision=loaded.revision;
            expect(loaded.sources.size()==10&&loaded.scene_id=="room","Modern source graph omitted a registered source");
            expect(ok(lm::check_scripts(loaded)),"Registered module checking failed");
            expect(ok(lm::checkpoint_project(modern_root,"v2")),"Version 2 graph checkpoint failed");
            write(modern_root/"data/items.json","{\"key\":{\"name\":\"Changed\"}}");
            expect(lm::load_project(modern_root).revision!=initial_revision,"Definition change did not revise graph");
            write(modern_root/"ui/font.bin","changed");
            expect(ok(lm::restore_project(modern_root,"v2")),"Modern scene checkpoint failed to restore original manifest metadata");
            expect(lm::load_project(modern_root).revision==initial_revision&&read(modern_root/"ui/font.bin")==binary,"Binary source graph restoration changed bytes");
            auto patched=lm::patch_project(modern_root,patch(initial_revision,Json::array({change("floor","position",{1,2,3})})));
            expect(ok(patched)&&Json::parse(read(modern_root/"project.json"))["entities"][1]["position"]==Json({0,0,0}),"Scene patch must preserve root manifest entities");
            expect(Json::parse(read(modern_root/"scenes/room.json"))["entities"][1]["position"]==Json({1,2,3}),"Scene patch did not edit active registered scene");
            loaded=lm::load_project(modern_root);lm::copy_project_sources(loaded,modern_root/"copy");
            expect(lm::load_project(modern_root/"copy").revision==loaded.revision,"Modern source clone differs");
            write(modern_root/"modules/math.luau","local x: number = 'bad'\nreturn {value=x}");
            expect(!ok(lm::check_scripts(lm::load_project(modern_root))),"Offline type checker missed module type error");
        }
        fs::remove_all(test_root);
        std::cout << "PASS: " << checks << " project validation, revision, transaction, checkpoint and example checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << "\nFixture preserved at " << test_root << '\n';
        return 1;
    }
}
