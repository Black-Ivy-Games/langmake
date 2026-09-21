#include "langmake/collision2d.hpp"
#include "langmake/world.hpp"
#include "langmake/project.hpp"
#include "langmake/mobile_platform.hpp"
#include "langmake/navigation.hpp"
#include "langmake/model.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <box2d/box2d.h>
#include <flecs.h>
#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <chrono>
#ifdef LANGMAKE_MOBILE
#include <time.h>
#endif
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <utility>

namespace lm {
namespace {
constexpr JPH::ObjectLayer static_layer = 0;
constexpr JPH::ObjectLayer dynamic_layer = 1;

struct JoltRegistration {
    JoltRegistration() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ~JoltRegistration() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer.GetValue() == 0 ? "Static" : "Dynamic";
    }
#endif
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == dynamic_layer || b == dynamic_layer;
    }
};

class ObjectBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return a == dynamic_layer || b.GetValue() == dynamic_layer;
    }
};

// Keep physics-owned state in Flecs and presentation state in Record.
struct RuntimeState {
    Vec3 position;
    Color color;
    bool visible;
    bool collected;
};

struct EcsCloser {
    void operator()(ecs_world_t* world) const { if (world) ecs_fini(world); }
};
struct LuaCloser {
    void operator()(lua_State* state) const { if (state) lua_close(state); }
};
struct BoxWorld {
    b2WorldId id = b2_nullWorldId;
    ~BoxWorld() { if (b2World_IsValid(id)) b2DestroyWorld(id); }
};

bool finite_vec(const Vec3& value) {
    return std::all_of(value.begin(), value.end(), [](float v) { return std::isfinite(v); });
}
}

struct World::Impl {
    struct Record {
        Entity source;
        ecs_entity_t ecs_id = 0;
        b2BodyId box_body = b2_nullBodyId;
        JPH::BodyID jolt_body;
        double animation_start = 0;
        double previous_animation_start = 0;
        double blend_start = 0;
        double blend_duration = 0;
        bool alive = true;
        bool pending_create = false;
        bool pending_destroy = false;
        std::string prefab;
        std::string instance;
        Json animation_events = Json::array();
        std::unique_ptr<JPH::CharacterVirtual> character;
        Vec3 character_velocity{0, 0, 0};
        Vec3 movement_velocity{0,0,0};
        bool character_grounded=false;
        float spin=0; // degrees per second, retained while collision is disabled
        Vec3 local_position{0,0,0},local_rotation{0,0,0},local_scale{1,1,1};
    };

    Project project;
    CompiledAssets compiled;
    std::unique_ptr<ecs_world_t, EcsCloser> ecs;
    ecs_entity_t state_component = 0;
    BoxWorld box;
    BroadPhaseLayers broad_layers;
    ObjectPairs pair_filter;
    ObjectBroadPhase broad_filter;
    std::unique_ptr<JPH::TempAllocatorImpl> temporary;
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
    std::unique_ptr<JPH::PhysicsSystem> physics;
    // Records own CharacterVirtual objects, which need PhysicsSystem during destruction,
    // including when construction fails before Impl's destructor can run.
    std::vector<Record> records;
    std::unordered_map<std::string, std::size_t> index;
    std::size_t player_index = std::numeric_limits<std::size_t>::max();

    struct ScriptMemory {
        std::size_t used = 0;
        static constexpr std::size_t limit = 16 * 1024 * 1024;
    } script_memory;
    std::unique_ptr<lua_State, LuaCloser> script;
    int script_table = LUA_NOREF;
    std::chrono::steady_clock::time_point deadline;
#ifdef LANGMAKE_MOBILE
    std::int64_t cpu_deadline_ns = 0;
#endif
    bool executing_script = false;
    bool budget_exceeded = false;
    Input input;
    Input previous_input;
    std::uint64_t tick = 0;
    double elapsed = 0;
    std::uint64_t audio_serial = 0;
    unsigned audio_count = 0;
    Json sounds = Json::array();
    std::string current_music;
    std::string current_music_bus="music";
    bool current_music_loop=true;
    bool did_win = false;
    int collection_count = 0;
    std::string status_message;
    Json game_state = Json::object();
    Json ui_state = Json::object();
    Json viewport_state = {{"available",false}};
    Json buses = Json::object();
    Json contacts = Json::array();
    std::set<std::pair<std::string,std::string>> contact_pairs,contact_candidates;
    struct Listener final:JPH::ContactListener {
        Impl* world;
        explicit Listener(Impl* owner):world(owner){}
        void OnContactAdded(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold&,JPH::ContactSettings&)override{world->record_contact(a.GetID(),b.GetID());}
        void OnContactPersisted(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold&,JPH::ContactSettings&)override{world->record_contact(a.GetID(),b.GetID());}
    } contact_listener{this};
    Json camera = Json::object();
    Json last_input_events = Json::array();
    Json session_requests = Json::array();
    Json scene_states = Json::object();
    std::map<std::string, std::shared_ptr<Project>> scene_catalog;
    std::unordered_map<std::string, int> loaded_modules;
    std::unordered_set<std::string> loading_modules;
    const Json* migration_source=nullptr;
    int migration_from=1,migration_to=1;
    std::uint64_t spawn_serial = 0;
    std::uint64_t random_state = 0x853c49e6748fea9bull;
    std::string requested_scene;
    std::string requested_save;
    std::unique_ptr<Navigation> navigation;
    std::map<std::string, Json> nav_agents;
    Json tick_statistics = Json::object();

    explicit Impl(const Project& source, bool start = true) : project(source), compiled(compile_assets(source)), ecs(ecs_init()), current_music(source.music), scene_catalog(source.scenes) {
        project.save_fingerprint=save_structure_fingerprint(source);
      try {
        if(!scene_catalog.empty()&&!scene_catalog.contains(project.scene_id)){
            auto active=std::make_shared<Project>(source);active->scenes.clear();scene_catalog.emplace(project.scene_id,std::move(active));
        }
        if (!ecs) throw std::runtime_error("Could not create Flecs world");
        if (project.dimension != "2d" && project.dimension != "3d")
            throw std::runtime_error("World dimension must be '2d' or '3d'");
        if (!finite_vec(project.gravity)) throw std::runtime_error("Gravity must be finite");
        if (project.entities.size() > 4096) throw std::runtime_error("World entity limit is 4096");

        ecs_component_desc_t component{};
        component.type.size = sizeof(RuntimeState);
        component.type.alignment = alignof(RuntimeState);
        state_component = ecs_component_init(ecs.get(), &component);
        records.reserve(4096);
        for (const auto& entity : project.entities) {
            if (entity.id.empty() || index.contains(entity.id))
                throw std::runtime_error("Entity IDs must be nonempty and unique: " + entity.id);
            if (!finite_vec(entity.position) || !finite_vec(entity.size) ||
                !std::all_of(entity.size.begin(), entity.size.end(), [](float v) { return v >= 0.01f && v <= 10000; }))
                throw std::runtime_error("Invalid entity dimensions or position: " + entity.id);
            if (!std::isfinite(entity.mass) || entity.mass <= 0)
                throw std::runtime_error("Entity mass must be positive: " + entity.id);
            if (entity.shape == "sphere" && entity.body != "none" &&
                (entity.size[0] != entity.size[1] || entity.size[0] != entity.size[2]))
                throw std::runtime_error("Collidable sphere size must be equal on all axes: " + entity.id);
            index.emplace(entity.id, records.size());
            if (entity.role == "player") {
                if (player_index != std::numeric_limits<std::size_t>::max())
                    throw std::runtime_error("World supports exactly one player");
                player_index = records.size();
            }
            Record record;
            record.source = entity;
            record.local_position=entity.position;record.local_rotation=entity.rotation;record.local_scale=entity.visual_scale;
            record.ecs_id = ecs_new(ecs.get());
            records.push_back(std::move(record));
            set_state(records.back(), {entity.position, entity.color, entity.visible, false});
        }
        update_hierarchy();
        if (project.dimension == "2d") create_box_world();
        else create_jolt_world();
        initialize_services();
        load_script(start);
        flush_entities();
      } catch (...) {
        if(physics)physics->SetContactListener(nullptr);
        for(auto& record:records)record.character.reset();
        throw;
      }
    }

    RuntimeState state(const Record& record) const {
        return *static_cast<const RuntimeState*>(ecs_get_id(ecs.get(), record.ecs_id, state_component));
    }
    void set_state(const Record& record, const RuntimeState& value) {
        ecs_set_id(ecs.get(), record.ecs_id, state_component, sizeof(value), &value);
    }
    bool has_body(const Record& record) const {
        return record.alive && !record.pending_create && !record.pending_destroy && record.source.collision_enabled &&
            record.source.body != "none";
    }
    Record* player() {
        return player_index < records.size() && records[player_index].alive && !records[player_index].pending_destroy ? &records[player_index] : nullptr;
    }
    const Record* player() const {
        return player_index < records.size() && records[player_index].alive && !records[player_index].pending_destroy ? &records[player_index] : nullptr;
    }

    void create_box_world() {
        b2WorldDef def = b2DefaultWorldDef();
        def.gravity = {project.gravity[0], project.gravity[1]};
        box.id = b2CreateWorld(&def);
        for (auto& record : records) create_body(record);
    }

    void create_body(Record& record) {
        validate_collision2d_entity(record.source,project.dimension);
        if (!has_body(record)) return;
        const auto& entity = record.source;
        const auto position = state(record).position;
        if (project.dimension == "2d") {
            b2BodyDef body = b2DefaultBodyDef();
            body.type = entity.body == "dynamic" ? b2_dynamicBody : b2_staticBody;
            if(entity.body=="kinematic")body.type=b2_kinematicBody;
            if(entity.controller=="character")body.type=b2_kinematicBody;
            body.position = {position[0], position[1]};
            body.fixedRotation = !entity.physics2d.value("rotate",false);
            body.rotation=b2MakeRot(entity.physics2d.empty()?0:entity.rotation[2]*3.14159265f/180);
            body.angularVelocity=record.spin*3.14159265f/180;
            body.angularDamping=entity.physics2d.value("angular_damping",.2f);
            body.enableSleep = false;
            record.box_body = b2CreateBody(box.id, &body);
            b2ShapeDef shape = b2DefaultShapeDef();
            shape.material.friction = entity.physics2d.value("friction",.4f);
            shape.material.restitution=entity.physics2d.value("restitution",0.0f);
            shape.enableContactEvents=true;
            shape.enableSensorEvents=true;shape.isSensor=entity.body=="trigger";
            if(entity.controller=="character")shape.filter.categoryBits=2;
            if(entity.body=="trigger")shape.filter.categoryBits=4;
            shape.density = entity.mass / (entity.size[0] * entity.size[1]);
            if(entity.physics2d.contains("polygons")) {
                float area=0;
                for(const auto& part:entity.physics2d["polygons"])for(size_t i=0;i<part.size();++i) {
                    const auto& a=part[i];const auto& b=part[(i+1)%part.size()];
                    area+=(a[0].get<float>()*b[1].get<float>()-a[1].get<float>()*b[0].get<float>())*.5f*entity.size[0]*entity.size[1];
                }
                shape.density=entity.mass/area;
                for(const auto& part:entity.physics2d["polygons"]) {
                    b2Vec2 vertices[8];int n=0;for(const auto& v:part)vertices[n++]={v[0].get<float>()*entity.size[0],v[1].get<float>()*entity.size[1]};
                    auto hull=b2ComputeHull(vertices,n);if(hull.count<3)throw std::runtime_error("physics2d polygon is too small for Box2D");
                    auto polygon=b2MakePolygon(&hull,0);b2CreatePolygonShape(record.box_body,&shape,&polygon);
                }
            } else if (entity.shape == "capsule") {
                const float radius = entity.size[0] * .5f;
                const float half = std::max(0.0f, entity.size[1]*.5f-radius);
                b2Capsule capsule{{0,-half},{0,half},radius};
                b2CreateCapsuleShape(record.box_body,&shape,&capsule);
            } else if (entity.shape == "sphere" || entity.shape == "circle") {
                b2Circle circle{{0, 0}, entity.size[0] * 0.5f};
                shape.density = entity.mass / (3.14159265f * circle.radius * circle.radius);
                b2CreateCircleShape(record.box_body, &shape, &circle);
            } else {
                b2Polygon polygon = b2MakeBox(entity.size[0] * 0.5f, entity.size[1] * 0.5f);
                b2CreatePolygonShape(record.box_body, &shape, &polygon);
            }
            return;
        }
            JPH::RefConst<JPH::Shape> shape;
            if (entity.shape == "capsule")
                shape = new JPH::CapsuleShape(std::max(.001f,(entity.size[1]-entity.size[0])*.5f), entity.size[0]*.5f);
            else if (entity.shape == "sphere" || entity.shape == "circle")
                shape = new JPH::SphereShape(entity.size[0] * 0.5f);
            else
                shape = new JPH::BoxShape(JPH::Vec3(entity.size[0] * 0.5f, entity.size[1] * 0.5f, entity.size[2] * 0.5f), 0.001f);
            if (entity.controller == "character") {
                JPH::CharacterVirtualSettings settings;
                settings.mShape=shape;
                settings.mInnerBodyShape=shape;
                settings.mInnerBodyLayer=dynamic_layer;
                settings.mMass=entity.mass;
                settings.mMaxSlopeAngle=JPH::DegreesToRadians(45.0f);
                record.character = std::make_unique<JPH::CharacterVirtual>(&settings,
                    JPH::RVec3(position[0],position[1],position[2]),JPH::Quat::sIdentity(),physics.get());
                record.jolt_body = record.character->GetInnerBodyID();
                return;
            }
            const bool dynamic = entity.body == "dynamic";
            JPH::BodyCreationSettings settings(shape,
                JPH::RVec3(position[0], position[1], position[2]), JPH::Quat::sIdentity(),
                dynamic ? JPH::EMotionType::Dynamic : entity.body=="kinematic"?JPH::EMotionType::Kinematic:JPH::EMotionType::Static,
                dynamic || entity.body=="kinematic" || entity.body=="trigger" ? dynamic_layer : static_layer);
            settings.mIsSensor=entity.body=="trigger";
            if(entity.body=="trigger")settings.mCollideKinematicVsNonDynamic=true;
            settings.mFriction = 0.4f;
            settings.mAllowSleeping = false;
            settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
            if (dynamic) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = entity.mass;
                settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
            }
            record.jolt_body = physics->GetBodyInterface().CreateAndAddBody(settings,
                dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            if (record.jolt_body.IsInvalid()) throw std::runtime_error("Could not create body: " + entity.id);
    }

    void create_jolt_world() {
        static JoltRegistration registration;
        (void)registration;
        temporary = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
        jobs = std::make_unique<JPH::JobSystemSingleThreaded>(2048);
        physics = std::make_unique<JPH::PhysicsSystem>();
        physics->Init(8192, 0, 32768, 16384, broad_layers, broad_filter, pair_filter);
        physics->SetGravity(JPH::Vec3(project.gravity[0], project.gravity[1], project.gravity[2]));
        physics->SetContactListener(&contact_listener);
        for (auto& record : records) create_body(record);
        physics->OptimizeBroadPhase();
    }

    void sync_physics() {
        for (auto& record : records) {
            if (!has_body(record)) continue;
            auto current = state(record);
            if (project.dimension == "2d") {
                const auto position = b2Body_GetPosition(record.box_body);
                current.position[0] = position.x;
                current.position[1] = position.y;
                if(record.source.physics2d.value("rotate",false)) {
                    record.source.rotation[2]=b2Rot_GetAngle(b2Body_GetRotation(record.box_body))*180/3.14159265f;
                    record.local_rotation=record.source.rotation;record.spin=b2Body_GetAngularVelocity(record.box_body)*180/3.14159265f;
                }
            } else {
                const auto position = record.character ? record.character->GetPosition() : physics->GetBodyInterface().GetPosition(record.jolt_body);
                current.position = {static_cast<float>(position.GetX()), static_cast<float>(position.GetY()), static_cast<float>(position.GetZ())};
            }
            set_state(record, current);
        }
    }

    Vec3 velocity(const Record& record) const {
        if (record.source.body != "dynamic"&&record.source.body!="kinematic") return {0, 0, 0};
        if (record.character || record.source.controller=="character" || !has_body(record)) return record.character_velocity;
        if (project.dimension == "2d") {
            const auto value = b2Body_GetLinearVelocity(record.box_body);
            return {value.x, value.y, 0};
        }
        const auto value = physics->GetBodyInterface().GetLinearVelocity(record.jolt_body);
        return {value.GetX(), value.GetY(), value.GetZ()};
    }
    void set_velocity(Record& record, const Vec3& value) {
        if (record.source.body != "dynamic"&&record.source.body!="kinematic") return;
        record.character_velocity=value;
        record.movement_velocity=value;
        if (!has_body(record)) return;
        if (record.character) { record.character->SetLinearVelocity(JPH::Vec3(value[0],value[1],value[2])); return; }
        if(project.dimension=="2d"&&record.source.controller=="character")return;
        if (project.dimension == "2d") b2Body_SetLinearVelocity(record.box_body, {value[0], value[1]});
        else physics->GetBodyInterface().SetLinearVelocity(record.jolt_body, JPH::Vec3(value[0], value[1], value[2]));
    }

    // This slice supports unrotated primitive bodies. A narrow foot/support test
    // avoids accepting side-wall contacts as ground; the solvers own collisions.
    bool grounded(const Record& moving) const {
        if(!has_body(moving))return false;
        if (moving.character) return moving.character->IsSupported();
        if(moving.source.controller=="character"&&project.dimension=="2d")return moving.character_grounded;
        if (velocity(moving)[1] > 0.5f) return false;
        const auto current = state(moving);
        const auto& size = moving.source.size;
        const float feet = current.position[1] - size[1] * 0.5f;
        for (const auto& support : records) {
            if (support.ecs_id == moving.ecs_id || !has_body(support)) continue;
            if(support.source.body=="trigger")continue;
            const auto other = state(support);
            if (other.collected) continue;
            const float top = other.position[1] + support.source.size[1] * 0.5f;
            if (std::abs(feet - top) > 0.12f) continue;
            if (std::abs(current.position[0] - other.position[0]) >= (size[0] + support.source.size[0]) * 0.5f - 0.025f) continue;
            if (project.dimension == "3d" &&
                std::abs(current.position[2] - other.position[2]) >= (size[2] + support.source.size[2]) * 0.5f - 0.025f) continue;
            return true;
        }
        return false;
    }

    void teleport(Record& record, const Vec3& position) {
        if(!record.source.parent.empty()){
            auto parent=index.find(record.source.parent);if(parent==index.end())throw std::runtime_error("Entity parent is missing");
            const auto& owner=records[parent->second];const auto anchor=parent_anchor(owner,record.source.parent_joint);
            const auto local=anchor.rotation.Conjugated()*JPH::Vec3(position[0]-anchor.position[0],position[1]-anchor.position[1],position[2]-anchor.position[2]);
            record.local_position={local.GetX()/anchor.scale[0],local.GetY()/anchor.scale[1],local.GetZ()/anchor.scale[2]};
        }else record.local_position=position;
        auto current = state(record);
        current.position = position;
        set_state(record, current);
        set_velocity(record, {0, 0, 0});
        if (!has_body(record)) return;
        if (project.dimension == "2d") b2Body_SetTransform(record.box_body, {position[0], position[1]}, record.source.physics2d.empty()?b2Rot_identity:b2MakeRot(record.source.rotation[2]*3.14159265f/180));
        else if (record.character) record.character->SetPosition(JPH::RVec3(position[0],position[1],position[2]));
        else physics->GetBodyInterface().SetPosition(record.jolt_body,
            JPH::RVec3(position[0], position[1], position[2]), JPH::EActivation::Activate);
    }

    static void* allocate_script(void* context, void* pointer, std::size_t old_size, std::size_t new_size) {
        auto& memory = *static_cast<ScriptMemory*>(context);
        if (!pointer) old_size = 0;
        if (new_size == 0) {
            std::free(pointer);
            memory.used -= old_size;
            return nullptr;
        }
        if (new_size > ScriptMemory::limit - (memory.used - old_size)) return nullptr;
        void* result = std::realloc(pointer, new_size);
        if (result) memory.used = memory.used - old_size + new_size;
        return result;
    }

    static Impl& owner(lua_State* state) {
        return *static_cast<Impl*>(lua_callbacks(state)->userdata);
    }
    static void interrupt(lua_State* state, int gc) {
        auto& self = owner(state);
        if (gc < 0 && self.executing_script && self.script_budget_expired()) {
            self.budget_exceeded = true;
            luaL_error(state, "script execution budget exceeded (50 ms)");
        }
    }
    bool script_budget_expired() const {
#ifdef LANGMAKE_MOBILE
        // Scheduling pauses and Android lifecycle interruptions are not Luau CPU work.
        // Keep the same execution limit without falsely rejecting a descheduled frame.
        timespec now{};
        if (cpu_deadline_ns && clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0)
            return std::int64_t(now.tv_sec) * 1000000000 + now.tv_nsec > cpu_deadline_ns;
#endif
        return std::chrono::steady_clock::now() > deadline;
    }
    static float number(lua_State* state, int index) {
        const double value = luaL_checknumber(state, index);
        if (!std::isfinite(value) || std::abs(value) > 1000000.0)
            luaL_error(state, "argument %d must be finite and within +/-1000000", index);
        return static_cast<float>(value);
    }
    static Record& entity_argument(lua_State* state, int argument) {
        const char* id = luaL_checkstring(state, argument);
        auto& self = owner(state);
        const auto found = self.index.find(id);
        if (found == self.index.end()) luaL_error(state, "unknown entity '%s'", id);
        if(!self.records[found->second].alive||self.records[found->second].pending_destroy)luaL_error(state,"entity '%s' has been destroyed",id);
        return self.records[found->second];
    }
    static Record& player_argument(lua_State* state) {
        auto* record = owner(state).player();
        if (!record || record->source.body != "dynamic") luaL_error(state, "game needs a dynamic entity with role 'player'");
        return *record;
    }

    static int script_input(lua_State* state) {
        const char* name = luaL_checkstring(state, 1);
        const auto& input = owner(state).input;
        if (std::strcmp(name, "move_x") == 0) lua_pushnumber(state, input.move_x);
        else if (std::strcmp(name, "move_z") == 0) lua_pushnumber(state, input.move_z);
        else if (std::strcmp(name, "jump") == 0) lua_pushboolean(state, input.jump);
        else if (std::strcmp(name, "fire") == 0) lua_pushboolean(state, input.fire);
        else if(std::strcmp(name,"pointer_x")==0)lua_pushnumber(state,input.pointer[0]);
        else if(std::strcmp(name,"pointer_y")==0)lua_pushnumber(state,input.pointer[1]);
        else if (input.actions.contains(name)) lua_pushnumber(state,input.actions.at(name));
        else if (owner(state).project.document.value("input_actions",Json::object()).contains(name)) lua_pushnumber(state,0);
        else lua_pushnumber(state,0);
        return 1;
    }
    static int script_move(lua_State* state) {
        auto& record = player_argument(state);
        const float x = number(state, 1);
        const float z = lua_isnoneornil(state, 2) ? 0 : number(state, 2);
        auto current = record.source.controller.empty()?owner(state).velocity(record):record.movement_velocity;
        current[0] = x;
        current[2] = z;
        owner(state).set_velocity(record, current);
        return 0;
    }
    static int script_jump(lua_State* state) {
        auto& record = player_argument(state);
        const float speed = number(state, 1);
        auto& self = owner(state);
        const bool accepted = speed > 0 && self.grounded(record);
        if (accepted) {
            auto current = record.source.controller.empty()?self.velocity(record):record.movement_velocity;
            current[1] = speed;
            self.set_velocity(record, current);
        }
        lua_pushboolean(state, accepted);
        return 1;
    }
    static int script_position(lua_State* state) {
        const auto position = owner(state).state(entity_argument(state, 1)).position;
        for (float value : position) lua_pushnumber(state, value);
        return 3;
    }
    static int script_distance(lua_State* state) {
        auto& self = owner(state);
        const auto a = self.state(entity_argument(state, 1)).position;
        const auto b = self.state(entity_argument(state, 2)).position;
        const double x = a[0] - b[0], y = a[1] - b[1];
        const double z = self.project.dimension == "2d" ? 0 : a[2] - b[2];
        lua_pushnumber(state, std::sqrt(x * x + y * y + z * z));
        return 1;
    }
    static int script_collect(lua_State* state) {
        auto& self = owner(state);
        auto& record = entity_argument(state, 1);
        if (record.source.body != "none")
            luaL_error(state, "collect() requires an entity with body='none'");
        auto current = self.state(record);
        const bool collected_now = !current.collected;
        if (collected_now) {
            current.collected = true;
            current.visible = false;
            self.set_state(record, current);
            ++self.collection_count;
        }
        lua_pushboolean(state, collected_now);
        return 1;
    }
    static int script_win(lua_State* state) {
        owner(state).did_win = true;
        return 0;
    }
    static int script_message(lua_State* state) {
        std::size_t size = 0;
        const char* text = luaL_checklstring(state, 1, &size);
        if (size > 4096) luaL_error(state, "message exceeds 4096 bytes");
        owner(state).status_message.assign(text, size);
        return 0;
    }
    static int script_color(lua_State* state) {
        auto& record = entity_argument(state, 1);
        const float r = number(state, 2), g = number(state, 3), b = number(state, 4);
        auto current = owner(state).state(record);
        current.color[0] = std::clamp(r, 0.0f, 1.0f);
        current.color[1] = std::clamp(g, 0.0f, 1.0f);
        current.color[2] = std::clamp(b, 0.0f, 1.0f);
        owner(state).set_state(record, current);
        return 0;
    }
    static int script_set_position(lua_State* state) {
        auto& record = entity_argument(state, 1);
        const Vec3 position{number(state, 2), number(state, 3), number(state, 4)};
        owner(state).teleport(record, position);
        return 0;
    }
    static int script_collected(lua_State* state) {
        lua_pushinteger(state, owner(state).collection_count);
        return 1;
    }

    static int script_visible(lua_State* state) {
        auto& self = owner(state);
        auto& record = entity_argument(state, 1);
        luaL_checktype(state, 2, LUA_TBOOLEAN);
        auto value = self.state(record);
        value.visible = lua_toboolean(state, 2) && !value.collected;
        self.set_state(record, value);
        return 0;
    }
    static int script_rotation(lua_State* state) {
        auto& record = entity_argument(state, 1);
        record.source.rotation = {number(state, 2), number(state, 3), number(state, 4)};
        record.local_rotation=record.source.rotation;
        if(!record.source.physics2d.empty()) {
            if(record.source.rotation[0]!=0||record.source.rotation[1]!=0)luaL_error(state,"physics2d only supports Z rotation");
            if(owner(state).has_body(record))b2Body_SetTransform(record.box_body,b2Body_GetPosition(record.box_body),b2MakeRot(record.source.rotation[2]*3.14159265f/180));
        }
        return 0;
    }
    static int script_get_rotation(lua_State* vm) {const auto& e=entity_argument(vm,1).source;for(float v:e.rotation)lua_pushnumber(vm,v);return 3;}
    static int script_spin(lua_State* vm) {lua_pushnumber(vm,entity_argument(vm,1).spin);return 1;}
    static int script_set_spin(lua_State* vm) {
        auto& self=owner(vm);auto& record=entity_argument(vm,1);const float value=number(vm,2);
        if(!record.source.physics2d.value("rotate",false)||std::abs(value)>3600)luaL_error(vm,"angular velocity requires rotating physics2d and -3600..3600 degrees/s");
        record.spin=value;if(self.has_body(record))b2Body_SetAngularVelocity(record.box_body,value*3.14159265f/180);return 0;
    }
    static int script_velocity(lua_State* state) {
        auto& record = entity_argument(state, 1);
        if (record.source.body != "dynamic"&&record.source.body!="kinematic") luaL_error(state, "set_velocity requires a dynamic body or kinematic body");
        const Vec3 value{number(state, 2), number(state, 3), number(state, 4)};
        owner(state).set_velocity(record, value);
        return 0;
    }
    static int script_animation(lua_State* state) {
        auto& self = owner(state);
        auto& record = entity_argument(state, 1);
        const char* clip = luaL_checkstring(state, 2);
        const auto found = self.compiled.sprites.find(record.source.asset);
        const auto model = self.compiled.models.find(record.source.asset);
        if (found == self.compiled.sprites.end() && model == self.compiled.models.end()) luaL_error(state, "play_animation requires a sprite entity or animated model");
        if (found != self.compiled.sprites.end() && !found->second->animations.contains(clip)) luaL_error(state, "unknown sprite animation '%s'", clip);
        if (model != self.compiled.models.end() && !model->second->animations.contains(clip)) luaL_error(state, "unknown model animation '%s'", clip);
        const double blend=lua_isnoneornil(state,4)?0:number(state,4);
        if(blend<0||blend>10)luaL_error(state,"animation blend duration must be 0..10 seconds");
        if (record.source.animation != clip || lua_toboolean(state, 3)) {
            record.source.animation_previous=record.source.animation;record.previous_animation_start=record.animation_start;
            record.blend_start=self.elapsed;record.blend_duration=blend;record.animation_start = self.elapsed;
        }
        record.source.animation = clip;
        return 0;
    }
    void emit_sound(lua_State* state, const char* action, const char* asset, bool loop) {
        if (++audio_count > 32) luaL_error(state, "audio event limit is 32 per update");
        sounds.push_back({{"serial", ++audio_serial}, {"tick", tick}, {"scene",project.scene_id}, {"action", action}, {"asset", asset}, {"loop", loop}});
        if (sounds.size() > 128) sounds.erase(sounds.begin());
    }
    static int script_sound(lua_State* state) {
        auto& self = owner(state);
        const char* asset = luaL_checkstring(state, 1);
        if (!self.compiled.music.contains(asset)) luaL_error(state, "unknown music asset '%s'", asset);
        const std::string bus=lua_isnoneornil(state,2)?"sfx":luaL_checkstring(state,2);
        if(!self.buses.contains(bus))luaL_error(state,"unknown audio bus '%s'",bus.c_str());
        self.emit_sound(state, "sound", asset, false);
        self.sounds.back()["bus"]=bus;
        return 0;
    }
    static int script_music(lua_State* state) {
        auto& self = owner(state);
        const char* asset = luaL_checkstring(state, 1);
        if (!self.compiled.music.contains(asset)) luaL_error(state, "unknown music asset '%s'", asset);
        const bool loop = lua_isnoneornil(state, 2) || lua_toboolean(state, 2);
        const std::string bus=lua_isnoneornil(state,3)?"music":luaL_checkstring(state,3);
        if(!self.buses.contains(bus))luaL_error(state,"unknown audio bus '%s'",bus.c_str());
        self.current_music = asset;
        self.current_music_bus=bus;self.current_music_loop=loop;
        self.emit_sound(state, "music", asset, loop);
        self.sounds.back()["bus"]=bus;
        return 0;
    }
    static int script_stop_music(lua_State* state) {
        auto& self = owner(state);
        self.current_music.clear();
        self.emit_sound(state, "stop_music", "", false);
        return 0;
    }

    [[noreturn]] void script_error(const char* phase, int code) {
        // A script may catch a VM budget exception and leave no result on the
        // stack. Never inspect stack index -1 unless an error object exists.
        const char* error = budget_exceeded ? "script execution budget exceeded (50 ms)" :
            lua_gettop(script.get()) > 0 ? lua_tostring(script.get(), -1) : nullptr;
        Json details{{"code", budget_exceeded ? "SCRIPT_BUDGET_EXCEEDED" : code == LUA_ERRMEM ? "SCRIPT_MEMORY_LIMIT" : "SCRIPT_ERROR"},
            {"phase", phase}, {"file", project.script_file.generic_string()}, {"tick", tick},
            {"message", error ? error : "unknown Luau error"}, {"source_revision", project.revision}};
        lua_settop(script.get(), 0);
        throw std::runtime_error(details.dump());
    }
    void protected_call(const char* phase, int arguments, int results) {
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
#ifdef LANGMAKE_MOBILE
        timespec began{};
        cpu_deadline_ns = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &began) == 0
            ? std::int64_t(began.tv_sec) * 1000000000 + began.tv_nsec + 50000000 : 0;
#endif
        executing_script = true;
        budget_exceeded = false;
        const int code = lua_pcall(script.get(), arguments, results, 0);
        executing_script = false;
        if (code != LUA_OK || budget_exceeded) script_error(phase, code);
    }
    #include "world_services.inc"

    void load_script(bool start = true) {
        if (project.script_source.size() > 1024 * 1024)
            throw std::runtime_error("Script source exceeds 1 MiB limit");
        script.reset(lua_newstate(allocate_script, &script_memory));
        if (!script) throw std::runtime_error("Could not initialize Luau VM");
        auto* vm = script.get();
        lua_callbacks(vm)->userdata = this;
        lua_callbacks(vm)->interrupt = interrupt;
        luaL_openlibs(vm);
        lua_newtable(vm);
        const std::pair<const char*, lua_CFunction> functions[] = {
            {"input", script_input}, {"move_player", script_move}, {"jump", script_jump},
            {"position", script_position}, {"distance", script_distance}, {"collect", script_collect},
            {"win", script_win}, {"message", script_message}, {"set_color", script_color},
            {"set_position", script_set_position}, {"collected", script_collected},
            {"rotation",script_get_rotation},{"angular_velocity",script_spin},{"set_angular_velocity",script_set_spin},{"set_visible", script_visible}, {"set_rotation", script_rotation}, {"set_velocity", script_velocity},
            {"play_animation", script_animation}, {"play_sound", script_sound},
            {"play_music", script_music}, {"stop_music", script_stop_music},
            {"state_get",script_state_get},{"state_set",script_state_set},
            {"spawn",script_spawn},{"destroy",script_destroy},{"exists",script_exists},
            {"set_asset",script_asset},{"set_visual_scale",script_scale},{"set_opacity",script_opacity},
            {"set_collision_enabled",script_collision},{"raycast",script_raycast},{"overlap",script_overlap},{"shape_cast",script_raycast},
            {"input_pressed",script_pressed},{"input_released",script_released},{"input_events",script_events},
            {"ui_set",script_ui_set},{"ui_get",script_ui_get},{"ui_event",script_ui_event},
            {"change_scene",script_scene},{"scene",script_scene_id},
            {"camera_set",script_camera},{"camera_get",script_camera_get},{"viewport",script_viewport},
            {"navigation_path",script_nav_path},{"navigate",script_navigate},{"navigation_stop",script_nav_stop},
            {"navigation_status",script_nav_status},{"navigation_rebuild",script_nav_rebuild},
            {"set_audio_bus",script_bus},{"random",script_random},{"tick",script_tick},
            {"definitions",script_definitions},{"entities",script_entities},{"grounded",script_grounded},
            {"save_game",script_save_game},{"load_game",script_load_game},
            {"animation_events",script_animation_events},{"joint_position",script_joint_position},{"contacts",script_contacts}};
        for (const auto& [name, function] : functions) {
            lua_pushcfunction(vm, function, name);
            lua_setfield(vm, -2, name);
        }
        lua_setreadonly(vm, -1, true);
        lua_setglobal(vm, "engine");
        lua_pushcfunction(vm,script_require,"require");
        lua_setglobal(vm,"require");
        // No print output may corrupt the machine-readable CLI stream.
        lua_pushnil(vm);
        lua_setglobal(vm, "print");
        luaL_sandbox(vm);
        luaL_sandboxthread(vm);
        const std::string source = project.script_source.empty() ? "return {}" : project.script_source;
        const std::string bytecode = Luau::compile(source);
        if (bytecode.size() > 8 * 1024 * 1024) throw std::runtime_error("Compiled script exceeds 8 MiB limit");
        const std::string chunk = "@" + project.script_file.generic_string();
        const int code = luau_load(vm, chunk.c_str(), bytecode.data(), bytecode.size(), 0);
        if (code != LUA_OK) script_error("compile", code);
        protected_call("load", 0, 1);
        if (!lua_istable(vm, -1)) {
            lua_pushliteral(vm, "game script must return a table with optional start() and update(dt) functions");
            script_error("load", LUA_ERRRUN);
        }
        script_table = lua_ref(vm, -1);
        lua_pop(vm, 1);
        if (start) call_hook("start", false, 0);
    }
    void call_hook(const char* name, bool with_dt, float dt) {
        auto* vm = script.get();
        lua_getref(vm, script_table);
        // Look up hooks without invoking user metatables outside lua_pcall.
        lua_rawgetfield(vm, -1, name);
        lua_remove(vm, -2);
        if (lua_isnil(vm, -1)) { lua_pop(vm, 1); return; }
        if (!lua_isfunction(vm, -1)) {
            lua_pushfstring(vm, "script field '%s' must be a function", name);
            script_error(name, LUA_ERRRUN);
        }
        if (with_dt) lua_pushnumber(vm, dt);
        protected_call(name, with_dt ? 1 : 0, 0);
    }
    void step(float dt, const Input& next_input) {
        if (!std::isfinite(dt) || dt <= 0 || dt > 0.1f)
            throw std::runtime_error("Simulation dt must be finite and in (0, 0.1]");
        if (!std::isfinite(next_input.move_x) || !std::isfinite(next_input.move_z))
            throw std::runtime_error("Input axes must be finite");
        const auto began=std::chrono::steady_clock::now();
        previous_input = input;
        input = next_input;
        last_input_events=input.events;
        input.move_x = std::clamp(input.move_x, -1.0f, 1.0f);
        input.move_z = std::clamp(input.move_z, -1.0f, 1.0f);
        audio_count = 0;
        call_hook("update", true, dt);
        const auto after_script=std::chrono::steady_clock::now();
        flush_entities();
        update_navigation(dt);
        const auto after_navigation=std::chrono::steady_clock::now();
        contact_candidates.clear();
        update_characters(dt);
        if (project.dimension == "2d") b2World_Step(box.id, dt, 4);
        else {
            const auto result = physics->Update(dt, 1, temporary.get(), jobs.get());
            if (result != JPH::EPhysicsUpdateError::None)
                throw std::runtime_error("Jolt physics capacity exceeded");
        }
        sync_physics();
        update_contacts();
        const auto after_physics=std::chrono::steady_clock::now();
        update_animation_events(dt);
        ++tick;
        elapsed += dt;
        update_hierarchy();
        update_camera(dt);
        const auto finished=std::chrono::steady_clock::now();
        tick_statistics={{"script_ms",std::chrono::duration<double,std::milli>(after_script-began).count()},
                         {"navigation_ms",std::chrono::duration<double,std::milli>(after_navigation-after_script).count()},
                         {"physics_ms",std::chrono::duration<double,std::milli>(after_physics-after_navigation).count()},
                         {"simulation_ms",std::chrono::duration<double,std::milli>(finished-began).count()}};
    }
};

World::World(const Project& project) : impl_(std::make_unique<Impl>(project)) {}
World::~World() = default;
void World::step(float dt, const Input& input) {
    impl_->step(dt, input);
    if(!impl_->requested_scene.empty()) { const auto next=std::exchange(impl_->requested_scene,{});change_scene(next); }
}
bool World::won() const { return impl_->did_win; }
int World::collected() const { return impl_->collection_count; }
std::string World::message() const { return impl_->status_message; }
const CompiledAssets& World::assets() const { return impl_->compiled; }
double World::time() const { return impl_->elapsed; }
Json World::audio_events() const { return impl_->sounds; }
Json World::take_requests() { return std::exchange(impl_->session_requests,Json::array()); }
const Project& World::current_project() const { return impl_->project; }
Json World::presentation() const { return {{"camera",impl_->camera},{"ui",impl_->ui_state},{"scene",impl_->project.scene_id},{"tick",impl_->tick},{"time",impl_->elapsed}}; }
void World::set_scene_prepare(std::function<void(const Project&, const CompiledAssets&)> prepare) { scene_prepare_=std::move(prepare); }
Json World::save() const {
    auto result=impl_->save_state();result["scene_states"]=impl_->scene_states;
    if(result.dump().size()>16*1024*1024)throw std::runtime_error("Save exceeds 16 MiB");return result;
}
void World::restore(const Json& saved) {
    Json data=saved;
    const auto project_id=impl_->project.document.is_object()?impl_->project.document.value("project_id",impl_->project.title):impl_->project.title;
    const int version=impl_->project.document.is_object()?impl_->project.document.value("save_version",1):1;
    if(!data.is_object()||data.value("format_version",0)!=1||data.dump().size()>16*1024*1024)throw std::runtime_error("Unsupported or oversized player save");
    if(data.value("project_id",std::string())!=project_id)throw std::runtime_error("Save belongs to another project");
    const auto compatibility=save_compatibility_report(impl_->project,saved);
    const bool presentation_reuse=compatibility["status"]=="presentation_compatible";
    if(presentation_reuse) {
        auto refresh=[&](Json& state,const Project& scene_project) {
            state["source_revision"]=impl_->project.revision;
            state["ui"]=scene_project.ui.value("data",Json::object());
            state["camera"]=Impl::authored_camera(scene_project);
        };
        const auto scene=data.value("scene",impl_->project.scene_id);
        if(scene==impl_->project.scene_id)refresh(data,impl_->project);
        else if(impl_->scene_catalog.contains(scene))refresh(data,*impl_->scene_catalog.at(scene));
        else throw std::runtime_error("Save references unknown scene");
        if(data.contains("scene_states")) {
            if(!data["scene_states"].is_object())throw std::runtime_error("Save scene_states must be an object");
            for(auto&[id,state]:data["scene_states"].items()) {
                if(!impl_->scene_catalog.contains(id))throw std::runtime_error("Save references unknown stored scene");
                auto scene_project=*impl_->scene_catalog.at(id);scene_project.scenes=impl_->scene_catalog;
                const auto assessed=save_compatibility_report(scene_project,state);
                if(!assessed.value("compatible",false))throw std::runtime_error("Stored scene requires explicit migration: "+id);
                refresh(state,scene_project);
            }
        }
    } else if(data.value("source_revision",std::string())!=impl_->project.revision||data.value("game_save_version",0)!=version){
        auto migration=std::make_unique<Impl>(impl_->project,false);
        data=migration->migrate_save(data,data.value("game_save_version",1),version);
        // Luau numbers cannot losslessly round-trip the RNG's 64-bit state.
        // Engine clock/identity metadata is immutable during content migration.
        for(const auto* key:{"tick","time","spawn_serial","random_state"})data[key]=saved.at(key);
        data["source_revision"]=impl_->project.revision;data["project_id"]=project_id;data["game_save_version"]=version;
        if(data.contains("scene_states")&&data["scene_states"].is_object())for(auto& [id,state]:data["scene_states"].items()){
            const auto& metadata=saved.contains("scene_states")&&saved["scene_states"].contains(id)?saved["scene_states"][id]:saved;
            for(const auto* key:{"tick","time","spawn_serial","random_state"})state[key]=metadata.at(key);
            state["source_revision"]=impl_->project.revision;state["project_id"]=project_id;state["game_save_version"]=version;
        }
    }
    const std::string scene=data.value("scene",std::string());
    Project project=impl_->project;
    if(scene!=project.scene_id){if(!impl_->scene_catalog.contains(scene))throw std::runtime_error("Save references unknown scene");project=*impl_->scene_catalog.at(scene);}
    project.scenes=impl_->scene_catalog;
    // Compatibility metadata describes current content after either migration or
    // assessed reuse. It never upgrades an old opt-out save merely by loading it.
    auto stamp=[&](Json& state,const Project& p) {
        state["save_compatibility"]=p.document.is_object()?p.document.value("save_compatibility",Json::object()):Json::object();
        state["save_compatibility"]["structural_fingerprint"]=save_structure_fingerprint(p);
        state["profile"]=p.profile;
    };
    stamp(data,project);
    if(data.contains("scene_states")&&data["scene_states"].is_object())for(auto&[id,state]:data["scene_states"].items())
        if(impl_->scene_catalog.contains(id)){auto scene_project=*impl_->scene_catalog.at(id);scene_project.scenes=impl_->scene_catalog;stamp(state,scene_project);}
    auto next=Impl::from_save(project,data,impl_->viewport_state);next->scene_states=data.value("scene_states",Json::object());
    if(!next->scene_states.is_object()||next->scene_states.size()>64)throw std::runtime_error("Save scene_states must be an object with at most 64 scenes");
    for(const auto& [id,state]:next->scene_states.items()){
        if(!next->scene_catalog.contains(id))throw std::runtime_error("Save references unknown stored scene");
        Impl::validate_saved_state(state,*next->scene_catalog.at(id));
        for(const auto& value:state["entities"]){const auto entity=Impl::entity_from_json(value);next->validate_visual(entity);}
    }
    next->audio_serial=impl_->audio_serial;
    for(auto& event:next->sounds)event["serial"]=++next->audio_serial;
    if(scene_prepare_)scene_prepare_(next->project,next->compiled);
    impl_.swap(next);
}
void World::change_scene(const std::string& scene) {
    if(!impl_->scene_catalog.contains(scene))throw std::runtime_error("Unknown scene '"+scene+"'");
    if(scene==impl_->project.scene_id)return;
    Project project=*impl_->scene_catalog.at(scene);project.scenes=impl_->scene_catalog;
    auto saved_scenes=impl_->scene_states;saved_scenes[impl_->project.scene_id]=impl_->save_state();
    if(saved_scenes.size()+(saved_scenes.contains(scene)?0:1)>64)throw std::runtime_error("Session supports at most 64 saved scenes including the active scene");
    std::unique_ptr<Impl> next;
    if(saved_scenes.contains(scene)){
        auto saved=saved_scenes[scene];saved["state"]=impl_->game_state;saved["tick"]=impl_->tick;saved["time"]=impl_->elapsed;
        saved["spawn_serial"]=impl_->spawn_serial;saved["random_state"]=impl_->random_state;next=Impl::from_save(project,saved,impl_->viewport_state);
    }else{
        next=std::make_unique<Impl>(project,false);next->game_state=impl_->game_state;next->tick=impl_->tick;next->elapsed=impl_->elapsed;
        next->spawn_serial=impl_->spawn_serial;next->random_state=impl_->random_state;next->viewport_state=impl_->viewport_state;
        for(auto& record:next->records){record.animation_start=next->elapsed;record.previous_animation_start=next->elapsed;record.blend_start=next->elapsed;}
        next->call_hook("start",false,0);next->flush_entities();
    }
    next->audio_serial=impl_->audio_serial;
    Json combined=impl_->sounds;
    for(auto& event:next->sounds){event["serial"]=++next->audio_serial;combined.push_back(event);}
    combined.push_back({{"serial",++next->audio_serial},{"tick",next->tick},{"action",next->current_music.empty()?"stop_music":"music"},
                       {"scene",next->project.scene_id},{"asset",next->current_music},{"loop",next->current_music_loop},{"bus",next->current_music_bus}});
    while(combined.size()>128)combined.erase(combined.begin());next->sounds=std::move(combined);
    Json requests=impl_->session_requests;for(const auto& request:next->session_requests)requests.push_back(request);next->session_requests=std::move(requests);
    next->scene_states=std::move(saved_scenes);
    if(scene_prepare_)scene_prepare_(next->project,next->compiled);
    impl_.swap(next);
}
Json World::statistics() const {
    auto result=impl_->tick_statistics;result["entities"]=impl_->index.size();result["entity_capacity"]=4096;
    result["script_memory_bytes"]=impl_->script_memory.used;result["modules_loaded"]=impl_->loaded_modules.size();
    result["navigation_agents"]=impl_->nav_agents.size();result["navigation"]=impl_->navigation?impl_->navigation->inspect():Json();
    result["physics_bodies"]=impl_->physics?impl_->physics->GetNumBodies():b2World_GetCounters(impl_->box.id).bodyCount;
    result["runtime_slots"]=impl_->records.size();
    result["character_controllers"]=std::count_if(impl_->records.begin(),impl_->records.end(),[](const auto& record){return record.alive&&record.source.controller=="character";});
    return result;
}

void World::set_viewport(int width,int height,float density,const std::array<int,4>& requested_safe) {
    if(width<1||height<1||width>65535||height>65535||!std::isfinite(density)||density<=0||density>16)
        throw std::runtime_error("Viewport requires pixel dimensions 1..65535 and density greater than 0 and at most 16");
    const auto safe=requested_safe==std::array<int,4>{}?std::array<int,4>{0,0,width,height}:requested_safe;
    if(safe[0]<0||safe[1]<0||safe[2]<1||safe[3]<1||safe[0]>width||safe[1]>height||safe[2]>width-safe[0]||safe[3]>height-safe[1])
        throw std::runtime_error("Viewport safe area must be a positive rectangle contained within the pixel viewport");
    impl_->viewport_state={{"available",true},{"width",width},{"height",height},{"density",density},{"safe_area",safe},
        {"logical_size",{double(width)/density,double(height)/density}},
        {"logical_safe_area",{double(safe[0])/density,double(safe[1])/density,double(safe[2])/density,double(safe[3])/density}}};
}

std::vector<Entity> World::entities() const {
    std::vector<Entity> result;
    result.reserve(impl_->records.size());
    for (const auto& record : impl_->records) {
        if(!record.alive||record.pending_destroy)continue;
        auto entity = record.source;
        const auto state = impl_->state(record);
        entity.position = state.position;
        entity.color = state.color;
        entity.visible = state.visible;
        const auto [visible,opacity]=impl_->appearance(record);entity.visible=visible;entity.color[3]=opacity;
        entity.animation_time = (impl_->elapsed - record.animation_start) * entity.animation_speed;
        entity.animation_previous_time=(impl_->elapsed-record.previous_animation_start)*entity.animation_speed;
        entity.animation_blend=record.blend_duration<=0?1.0f:static_cast<float>(std::clamp((impl_->elapsed-record.blend_start)/record.blend_duration,0.0,1.0));
        result.push_back(std::move(entity));
    }
    return result;
}

Json World::snapshot() const {
    Json result{{"tick", impl_->tick}, {"dimension", impl_->project.dimension},
        {"won", won()}, {"collected", collected()}, {"message", message()},
        {"source_revision", impl_->project.revision},{"authored_graph_revision",impl_->project.source_revision},{"profile",impl_->project.profile}, {"time", impl_->elapsed},
        {"presentation_profile",impl_->project.profile},{"viewport",impl_->viewport_state},
        {"music", impl_->current_music},{"music_bus",impl_->current_music_bus},{"music_loop",impl_->current_music_loop}, {"audio_events", impl_->sounds}, {"entities", Json::array()},
        {"scene",impl_->project.scene_id},{"state",impl_->game_state},{"ui",impl_->ui_state},{"camera",impl_->camera},
        {"audio_buses",impl_->buses},{"navigation_agents",impl_->nav_agents},{"input_events",impl_->last_input_events},
        {"statistics",{{"entities",impl_->index.size()},{"entity_capacity",4096},{"navigation_agents",impl_->nav_agents.size()},
                       {"modules_loaded",impl_->loaded_modules.size()}}},{"contacts",impl_->contacts}};
    result["asset_revisions"] = Json::object();
    for (const auto& [id, source] : impl_->project.assets) result["asset_revisions"][id] = source.revision;
    for (const auto& record : impl_->records) {
        if(!record.alive||record.pending_destroy)continue;
        const auto state = impl_->state(record);
        const auto [visible,opacity]=impl_->appearance(record);auto color=state.color;color[3]=opacity;
        result["entities"].push_back({{"id", record.source.id}, {"role", record.source.role},
            {"position", state.position}, {"velocity", impl_->velocity(record)}, {"color", color},
            {"visible", visible}, {"collected", state.collected},
            {"asset", record.source.asset}, {"animation", record.source.animation},
            {"rotation", record.source.rotation}, {"flip_x", record.source.flip_x}});
        result["entities"].back()["angular_velocity"]=record.spin;
        result["entities"].back()["physics2d"]=record.source.physics2d;
        result["entities"].back()["visual_scale"]=record.source.visual_scale;
        result["entities"].back()["collision_enabled"]=record.source.collision_enabled;
        result["entities"].back()["controller"]=record.source.controller;
        result["entities"].back()["prefab"]=record.prefab;
        result["entities"].back()["parent"]=record.source.parent;
        result["entities"].back()["parent_joint"]=record.source.parent_joint;
        const auto sprite = impl_->compiled.sprites.find(record.source.asset);
        if (sprite != impl_->compiled.sprites.end()) {
            const auto time = (impl_->elapsed - record.animation_start) * record.source.animation_speed;
            const auto frame = sprite_frame_at(*sprite->second, record.source.animation, time);
            result["entities"].back()["animation_frame"] = sprite->second->frames.at(frame).id;
            result["entities"].back()["animation_time"] = time;
        }
    }
    const auto* player = impl_->player();
    result["grounded"] = player ? impl_->grounded(*player) : false;
    return result;
}
}
