// World service implementation, included inside World::Impl. Kept separate so
// stable legacy bindings and physics glue remain readable during the migration.
~Impl() {
    // CharacterVirtual destroys its inner body; it must outlive neither PhysicsSystem nor its allocator.
    for (auto& record : records) record.character.reset();
    if(physics)physics->SetContactListener(nullptr);
}

static void push_json(lua_State* vm, const Json& value) {
    if(!lua_checkstack(vm,8))luaL_error(vm,"not enough script stack for JSON value");
    if (value.is_null()) lua_pushnil(vm);
    else if (value.is_boolean()) lua_pushboolean(vm,value.get<bool>());
    else if (value.is_number()) lua_pushnumber(vm,value.get<double>());
    else if (value.is_string()) { const auto& s=value.get_ref<const std::string&>(); lua_pushlstring(vm,s.data(),s.size()); }
    else {
        lua_newtable(vm);
        if (value.is_array()) {
            // Preserve empty arrays through save migration and state round trips.
            lua_newtable(vm);lua_pushboolean(vm,true);lua_setfield(vm,-2,"__langmake_json_array");
            lua_pushliteral(vm,"Langmake JSON array");lua_setfield(vm,-2,"__metatable");lua_setreadonly(vm,-1,true);lua_setmetatable(vm,-2);
            int i=1; for (const auto& element:value) { push_json(vm,element); lua_rawseti(vm,-2,i++); }
        } else for (const auto& [key,element]:value.items()) { push_json(vm,element); lua_setfield(vm,-2,key.c_str()); }
    }
}

static Json read_lua_json(lua_State* vm, int position, unsigned depth, std::set<const void*>& seen, std::size_t& count, std::size_t max_values, std::size_t max_bytes) {
    if(!lua_checkstack(vm,8))throw std::runtime_error("not enough script stack for state");
    if (++count>max_values || depth>16) throw std::runtime_error("state exceeds "+std::to_string(max_values)+" values or 16 nesting levels");
    const int type=lua_type(vm,position);
    if (type==LUA_TNIL) return nullptr;
    if (type==LUA_TBOOLEAN) return lua_toboolean(vm,position)!=0;
    if (type==LUA_TNUMBER) {
        const double number=lua_tonumber(vm,position);
        if (!std::isfinite(number)) throw std::runtime_error("state numbers must be finite");
        return number;
    }
    if (type==LUA_TSTRING) {
        size_t size=0; const char* value=lua_tolstring(vm,position,&size);
        if (size>max_bytes) throw std::runtime_error(max_bytes==1024*1024?"state string exceeds 1 MiB":"state string exceeds bound byte limit");
        return std::string(value,size);
    }
    if (type!=LUA_TTABLE) throw std::runtime_error("state accepts JSON-compatible values only");
    const void* identity=lua_topointer(vm,position);
    if (!seen.insert(identity).second) throw std::runtime_error("state cannot contain cycles");
    const int index=position<0 ? lua_gettop(vm)+position+1 : position;
    bool tagged_array=false;
    if(lua_getmetatable(vm,index)){lua_rawgetfield(vm,-1,"__langmake_json_array");tagged_array=lua_toboolean(vm,-1);lua_pop(vm,2);}
    Json object=Json::object(); std::map<int,Json> array;
    bool has_strings=false,has_numbers=false;
    lua_pushnil(vm);
    while(lua_next(vm,index)) {
        Json value=read_lua_json(vm,-1,depth+1,seen,count,max_values,max_bytes);
        if (lua_type(vm,-2)==LUA_TSTRING) {
            has_strings=true; size_t size=0;const char* key=lua_tolstring(vm,-2,&size);
            if(size>256) throw std::runtime_error("state keys exceed 256 bytes");
            object[std::string(key,size)]=std::move(value);
        } else if(lua_type(vm,-2)==LUA_TNUMBER) {
            has_numbers=true;const double key=lua_tonumber(vm,-2);
            if(key<1 || key>65536 || std::floor(key)!=key) throw std::runtime_error("state array indices must be contiguous positive integers");
            array.emplace(static_cast<int>(key),std::move(value));
        } else throw std::runtime_error("state keys must be strings or array indices");
        lua_pop(vm,1);
    }
    seen.erase(identity);
    if(has_strings&&(has_numbers||tagged_array)) throw std::runtime_error("state cannot mix array and object keys");
    if(!has_numbers&&!tagged_array) return object;
    Json result=Json::array(); int expected=1;
    for(auto& [key,value]:array) { if(key!=expected++) throw std::runtime_error("state arrays must be contiguous"); result.push_back(std::move(value)); }
    return result;
}
static Json json_argument(lua_State* vm,int position,std::size_t max_bytes=1024*1024,std::size_t max_values=65536) {
    std::set<const void*> seen; std::size_t count=0;
    auto value=read_lua_json(vm,position,0,seen,count,max_values,max_bytes);
    if(value.dump().size()>max_bytes) throw std::runtime_error("script JSON value exceeds byte limit");
    return value;
}
static std::string key_argument(lua_State* vm,int position) {
    size_t size=0;const char* value=luaL_checklstring(vm,position,&size);
    if(size==0||size>256) throw std::runtime_error("state key must contain 1..256 bytes");
    return std::string(value,size);
}
static void validate_data(const Json& value,unsigned depth=0){
    if(depth>16)throw std::runtime_error("saved data exceeds 16 nesting levels");
    if(value.is_number()&&!std::isfinite(value.get<double>()))throw std::runtime_error("saved data numbers must be finite");
    if(value.is_object())for(const auto& [key,child]:value.items()){if(key.size()>256)throw std::runtime_error("saved data key exceeds 256 bytes");validate_data(child,depth+1);}
    else if(value.is_array()){if(value.size()>65536)throw std::runtime_error("saved data array exceeds 65536 elements");for(const auto& child:value)validate_data(child,depth+1);}
}

static Json authored_camera(const Project& project) {
    Json camera={{"enabled",false},{"mode","fixed"},{"position",project.camera_position},{"target",project.camera_target},
        {"follow",""},{"offset",Vec3{0,5,8}},{"smoothing",8.0f},{"obstruction",true},{"target_entity",""},{"half_height",11.0f},{"fov",60.0f}};
    if(project.document.contains("game_camera")) {
        for(const auto&[key,value]:project.document["game_camera"].items())camera[key]=value;
        camera["enabled"]=true;
    }
    validate_camera(camera);return camera;
}
void initialize_services() {
    if(project.document.contains("seed")){
        const auto& seed=project.document["seed"];
        if(!seed.is_number_integer()||(seed.is_number_integer()&&!seed.is_number_unsigned()&&seed.get<std::int64_t>()<0)||seed.get<std::uint64_t>()>9007199254740991ull)
            throw std::runtime_error("seed must be an integer in 0..9007199254740991");
        const auto value=seed.get<std::uint64_t>();random_state=value==0?0x853c49e6748fea9bull:value;
    }
    if(project.ui.is_object())ui_state=project.ui.value("data",Json::object());
    if(!ui_state.is_object()||ui_state.dump().size()>1024*1024)throw std::runtime_error("initial UI data must be an object up to 1 MiB");
    camera=authored_camera(project);
    if(project.document.contains("state")) game_state=project.document["state"];
    if(!game_state.is_object()||game_state.dump().size()>1024*1024) throw std::runtime_error("initial state must be an object up to 1 MiB");
    validate_data(game_state);validate_data(ui_state);
    for(const auto* name:{"master","music","sfx"}) buses[name]={{"gain",1.0f},{"pan",0.0f},{"mute",false}};
    if(project.document.contains("audio")&&project.document["audio"].contains("buses")) {
        for(const auto& [name,value]:project.document["audio"]["buses"].items()) buses[name]=value;
    }
    if(buses.size()>8) throw std::runtime_error("audio supports at most 8 buses");
    if(project.document.contains("navigation")&&project.document["navigation"].value("enabled",true))
        navigation=std::make_unique<Navigation>(project.dimension,project.entities,project.document["navigation"]);
}

static int script_require(lua_State* vm) {
    try {
        auto& self=owner(vm);const std::string id=luaL_checkstring(vm,1);
        if(const auto found=self.loaded_modules.find(id);found!=self.loaded_modules.end()) { lua_getref(vm,found->second);return 1; }
        if(!self.project.modules.contains(id)) throw std::runtime_error("unknown registered module '"+id+"'");
        if(!self.loading_modules.insert(id).second) throw std::runtime_error("module dependency cycle at '"+id+"'");
        const auto& source=self.project.modules.at(id);
        if(source.size()>1024*1024) throw std::runtime_error("module source exceeds 1 MiB: "+id);
        Luau::CompileOptions options;options.typeInfoLevel=1;
        const std::string bytecode=Luau::compile(source,options);
        if(bytecode.size()>8*1024*1024) throw std::runtime_error("module bytecode exceeds 8 MiB: "+id);
        const std::string file=self.project.module_files.contains(id)?self.project.module_files.at(id).generic_string():id;
        int status=luau_load(vm,("@"+file).c_str(),bytecode.data(),bytecode.size(),0);
        if(status==LUA_OK) self.compile_native(vm);
        if(status==LUA_OK) status=call_with_trace(vm,0,1);
        self.loading_modules.erase(id);
        if(status==LUA_OK) {
            if(!lua_istable(vm,-1)) throw std::runtime_error("module must return a table: "+id);
            self.loaded_modules.emplace(id,lua_ref(vm,-1));return 1;
        }
    } catch(const std::exception& error) { luaL_error(vm,"%s",error.what()); }
    // Outside the C++ catch: retain the original Lua object/status rather than
    // converting OOM to an ordinary, prefixed runtime-error string.
    lua_error(vm);return 0;
}
static int script_state_get(lua_State* vm) {
    try { auto& self=owner(vm);const auto key=key_argument(vm,1);
        if(auto provider=self.state_providers.find(key);provider!=self.state_providers.end()){
            if(self.flushing_state)throw std::runtime_error("Recursive bound state access");
            self.flushing_state=true;lua_getref(vm,provider->second);
            const int status=call_with_trace(vm,0,1);self.flushing_state=false;
            if(status==LUA_OK){auto value=json_argument(vm,-1,8*1024*1024,1048576);lua_pop(vm,1);push_json(vm,value);return 1;}
        } else {
            if(self.game_state.contains(key)) push_json(vm,self.game_state[key]);else if(lua_gettop(vm)>=2) lua_pushvalue(vm,2);else lua_pushnil(vm);return 1;
        } }
    catch(const std::exception& error){owner(vm).flushing_state=false;luaL_error(vm,"%s",error.what());}
    lua_error(vm);return 0;
}
static int script_state_set(lua_State* vm) {
    BindingProbe probe(owner(vm),2);
    try { auto& self=owner(vm);const auto key=key_argument(vm,1);if(self.flushing_state||self.state_providers.contains(key))throw std::runtime_error("Unbind the state provider before replacing its state");auto candidate=self.game_state;const auto value=json_argument(vm,2);
        if(value.is_null())candidate.erase(key);else candidate[key]=value;
        if(candidate.dump().size()>1024*1024)throw std::runtime_error("game state exceeds 1 MiB");self.game_state=std::move(candidate);return 0; }
    catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;
}

static Entity entity_from_json(const Json& value) {
    if(!value.is_object())throw std::runtime_error("entity template must be an object");
    Entity e;e.id=value.value("id","");e.name=value.value("name",e.id);e.position=value.value("position",Vec3{0,0,0});
    e.size=value.value("size",Vec3{1,1,1});e.shape=value.value("shape",std::string("box"));e.body=value.value("body",std::string("none"));
    e.role=value.value("role",std::string("decoration"));e.asset=value.value("asset",std::string());e.animation=value.value("animation",std::string());
    e.color=value.value("color",e.asset.empty()?Color{.5f,.7f,.9f,1}:Color{1,1,1,1});e.mass=value.value("mass",1.0f);
    e.visible=value.value("visible",true);e.rotation=value.value("rotation",Vec3{0,0,0});e.visual_scale=value.value("visual_scale",Vec3{1,1,1});
    e.physics2d=value.value("physics2d",Json::object());validate_collision2d(e.physics2d);
    e.collision_enabled=value.value("collision_enabled",true);e.controller=value.value("controller",std::string());
    e.parent=value.value("parent",std::string());
    e.parent_joint=value.value("parent_joint",std::string());
    e.flip_x=value.value("flip_x",false);e.animation_speed=value.value("animation_speed",1.0f);
    if(!finite_vec(e.position)||!finite_vec(e.size)||!finite_vec(e.visual_scale)||!finite_vec(e.rotation)||
       !std::isfinite(e.mass)||e.mass<.001f||e.mass>100000||!std::isfinite(e.animation_speed)||e.animation_speed<0||e.animation_speed>100)
        throw std::runtime_error("invalid entity numeric values");
    for(float v:e.position)if(std::abs(v)>100000)throw std::runtime_error("entity position exceeds world bounds");
    for(float v:e.size)if(v<.01f||v>10000)throw std::runtime_error("entity size must be 0.01..10000");
    for(float v:e.visual_scale)if(v<.001f||v>1000)throw std::runtime_error("visual scale must be 0.001..1000");
    for(float v:e.color)if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("entity color must be 0..1");
    if(e.shape!="box"&&e.shape!="sphere"&&e.shape!="capsule")throw std::runtime_error("unsupported entity shape");
    if(e.body!="none"&&e.body!="static"&&e.body!="dynamic"&&e.body!="kinematic"&&e.body!="trigger")throw std::runtime_error("unsupported entity body");
    if(!e.parent.empty()&&e.body!="none")throw std::runtime_error("parented visual entities require body='none'");
    if(!e.parent_joint.empty()&&e.parent.empty())throw std::runtime_error("parent_joint requires a parent entity");
    if(!e.controller.empty()&&(e.controller!="character"||e.body!="dynamic"))throw std::runtime_error("character controller requires dynamic body");
    if(e.shape=="capsule"&&(e.size[1]<e.size[0]||e.size[0]!=e.size[2]))throw std::runtime_error("capsule requires y>=x=z");
    if(e.shape=="sphere"&&e.body!="none"&&(e.size[0]!=e.size[1]||e.size[0]!=e.size[2]))throw std::runtime_error("collidable spheres require equal sizes");
    return e;
}
static Json entity_json(const Entity& e) {
    return {{"id",e.id},{"name",e.name},{"position",e.position},{"size",e.size},{"shape",e.shape},{"body",e.body},{"role",e.role},
            {"physics2d",e.physics2d},{"asset",e.asset},{"animation",e.animation},{"color",e.color},{"mass",e.mass},{"visible",e.visible},{"rotation",e.rotation},
            {"visual_scale",e.visual_scale},{"collision_enabled",e.collision_enabled},{"controller",e.controller},
            {"flip_x",e.flip_x},{"animation_speed",e.animation_speed},{"parent",e.parent},{"parent_joint",e.parent_joint}};
}
static void validate_visual(const Entity& e,const Project& project,const CompiledAssets& compiled) {
    if(!e.asset.empty()&&!project.assets.contains(e.asset))throw std::runtime_error("unknown visual asset '"+e.asset+"'");
    if(!e.asset.empty()&&project.assets.at(e.asset).type=="music")throw std::runtime_error("music cannot be a visual asset");
    if(!e.animation.empty()&&compiled.sprites.contains(e.asset)&&!compiled.sprites.at(e.asset)->animations.contains(e.animation))
        throw std::runtime_error("unknown sprite animation '"+e.animation+"'");
    if(!e.animation.empty()&&compiled.models.contains(e.asset)&&!compiled.models.at(e.asset)->animations.contains(e.animation))
        throw std::runtime_error("unknown model animation '"+e.animation+"'");
}
void validate_visual(const Entity& e)const { validate_visual(e,project,compiled); }
void remove_body(Record& record) {
    if(record.character) {record.character.reset();record.jolt_body=JPH::BodyID();}
    else if(physics&&!record.jolt_body.IsInvalid()) {
        auto& bodies=physics->GetBodyInterface();if(bodies.IsAdded(record.jolt_body))bodies.RemoveBody(record.jolt_body);
        bodies.DestroyBody(record.jolt_body);record.jolt_body=JPH::BodyID();
    }
    if(b2Body_IsValid(record.box_body)){b2DestroyBody(record.box_body);record.box_body=b2_nullBodyId;}
}
void flush_entities() {
    bool changed=true;
    while(changed){changed=false;for(auto& record:records)if(record.alive&&!record.pending_destroy&&!record.source.parent.empty()){
        const auto parent=index.find(record.source.parent);if(parent==index.end()||records[parent->second].pending_destroy){record.pending_destroy=true;changed=true;}}}
    for(auto& record:records) {
        if(!record.alive)continue;
        if(record.pending_destroy){remove_body(record);index.erase(record.source.id);nav_agents.erase(record.source.id);dynamic_surfaces.erase(record.source.id);surface_sources.erase(record.source.id);
            ecs_delete(ecs.get(),record.ecs_id);record.alive=false;continue;}
        if(record.pending_create){record.pending_create=false;create_body(record);set_velocity(record,record.character_velocity);}
    }
    update_hierarchy();
}
struct ParentAnchor{Vec3 position;Vec3 scale;JPH::Quat rotation;};
ParentAnchor parent_anchor(const Record& parent,const std::string& joint)const{
    ParentAnchor anchor{state(parent).position,parent.source.visual_scale,JPH::Quat::sEulerAngles(JPH::Vec3(parent.source.rotation[0],parent.source.rotation[1],parent.source.rotation[2])*(3.14159265359f/180.0f))};
    if(joint.empty())return anchor;
    const auto found=compiled.models.find(parent.source.asset);if(found==compiled.models.end())throw std::runtime_error("parent_joint requires an animated model parent");
    const float blend=parent.blend_duration<=0?1.0f:static_cast<float>(std::clamp((elapsed-parent.blend_start)/parent.blend_duration,0.0,1.0));
    const auto pose=sample_model_pose(*found->second,parent.source.animation,(elapsed-parent.animation_start)*parent.source.animation_speed,
        parent.source.animation_previous,(elapsed-parent.previous_animation_start)*parent.source.animation_speed,blend);
    const auto matrix=model_joint_transform(*found->second,pose,joint);
    const auto offset=anchor.rotation*JPH::Vec3(matrix[12]*parent.source.size[0]*anchor.scale[0],matrix[13]*parent.source.size[1]*anchor.scale[1],matrix[14]*parent.source.size[2]*anchor.scale[2]);
    for(int axis=0;axis<3;++axis)anchor.position[axis]+=offset[axis];
    JPH::Vec3 columns[3]={JPH::Vec3(matrix[0],matrix[1],matrix[2]),JPH::Vec3(matrix[4],matrix[5],matrix[6]),JPH::Vec3(matrix[8],matrix[9],matrix[10])};
    for(int axis=0;axis<3;++axis){const float length=columns[axis].Length();if(length<.00001f)throw std::runtime_error("attachment joint scale is singular");anchor.scale[axis]*=length;columns[axis]/=length;}
    if(std::abs(columns[0].Dot(columns[1]))>.001f||std::abs(columns[0].Dot(columns[2]))>.001f||std::abs(columns[1].Dot(columns[2]))>.001f)
        throw std::runtime_error("sheared joint attachments are unsupported");
    if(columns[0].Cross(columns[1]).Dot(columns[2])<.99f)throw std::runtime_error("reflected joint attachments are unsupported");
    const JPH::Mat44 rotation(JPH::Vec4(columns[0],0),JPH::Vec4(columns[1],0),JPH::Vec4(columns[2],0),JPH::Vec4(0,0,0,1));
    anchor.rotation=anchor.rotation*rotation.GetQuaternion();return anchor;
}
void update_hierarchy(){
    std::vector<unsigned char> marks(records.size(),0);
    auto visit=[&](auto&& self,std::size_t slot,unsigned depth)->void{
        auto& record=records[slot];if(!record.alive||record.pending_destroy)return;
        if(depth>16)throw std::runtime_error("entity parent hierarchy exceeds 16 levels");
        if(marks[slot]==2)return;if(marks[slot]==1)throw std::runtime_error("entity parent hierarchy contains a cycle");marks[slot]=1;
        if(!record.source.parent.empty()){
            if(record.source.body!="none")throw std::runtime_error("parented visual entities require body='none'");
            const auto parent=index.find(record.source.parent);if(parent==index.end())throw std::runtime_error("unknown entity parent '"+record.source.parent+"'");
            self(self,parent->second,depth+1);const auto& owner=records[parent->second];
            const auto anchor=parent_anchor(owner,record.source.parent_joint);
            const bool nonuniform=std::abs(anchor.scale[0]-anchor.scale[1])>.0001f||std::abs(anchor.scale[0]-anchor.scale[2])>.0001f;
            if(nonuniform&&(record.local_rotation[0]!=0||record.local_rotation[1]!=0||record.local_rotation[2]!=0))
                throw std::runtime_error("rotated visual children require uniform parent scale (shear transforms are unsupported)");
            const auto rotation=anchor.rotation;
            const auto local_rotation=JPH::Quat::sEulerAngles(JPH::Vec3(record.local_rotation[0],record.local_rotation[1],record.local_rotation[2])*(3.14159265359f/180.0f));
            const auto offset=rotation*JPH::Vec3(record.local_position[0]*anchor.scale[0],record.local_position[1]*anchor.scale[1],record.local_position[2]*anchor.scale[2]);
            const auto origin=anchor.position;auto value=state(record);value.position={origin[0]+offset.GetX(),origin[1]+offset.GetY(),origin[2]+offset.GetZ()};set_state(record,value);
            const auto angles=(rotation*local_rotation).GetEulerAngles()*(180.0f/3.14159265359f);record.source.rotation={angles.GetX(),angles.GetY(),angles.GetZ()};
            for(int axis=0;axis<3;++axis)record.source.visual_scale[axis]=record.local_scale[axis]*anchor.scale[axis];
        }else{record.source.rotation=record.local_rotation;record.source.visual_scale=record.local_scale;}
        marks[slot]=2;
    };
    for(std::size_t slot=0;slot<records.size();++slot)visit(visit,slot,0);
}
std::pair<bool,float> appearance(const Record& record)const{
    bool visible=true;float opacity=1;const Record* current=&record;
    for(unsigned depth=0;current&&depth<=16;++depth){const auto value=state(*current);visible=visible&&value.visible&&current->alive&&!current->pending_destroy;opacity*=value.color[3];
        if(current->source.parent.empty())break;const auto parent=index.find(current->source.parent);current=parent==index.end()?nullptr:&records[parent->second];}
    return {visible,opacity};
}
std::string spawn(const std::string& prefab,const Json& overrides) {
    if(!project.prefabs.contains(prefab))throw std::runtime_error("unknown prefab '"+prefab+"'");
    if(!overrides.is_object())throw std::runtime_error("spawn overrides must be an object");
    const auto& definition=project.prefabs.at(prefab);
    Json templates=definition.contains("entities")?definition["entities"]:Json::array({definition});
    if(!templates.is_array()||templates.empty()||templates.size()>256)throw std::runtime_error("prefab needs 1..256 entities");
    if(index.size()+templates.size()>4096)throw std::runtime_error("World entity limit is 4096");
    std::string instance;
    do{instance="runtime_"+std::to_string(++spawn_serial);}while(index.contains(instance));
    const Vec3 offset=overrides.value("position",Vec3{0,0,0});
    std::vector<Entity> prepared;std::set<std::string> ids;std::map<std::string,std::string> local_ids;
    for(std::size_t i=0;i<templates.size();++i){const auto local=templates[i].value("id",std::string("part_")+std::to_string(i));
        if(!local_ids.emplace(local,i==0?instance:instance+"_"+local).second)throw std::runtime_error("prefab local IDs must be unique");}
    const auto child_overrides=overrides.value("entities",Json::object());if(!child_overrides.is_object())throw std::runtime_error("prefab child overrides must be an object");
    for(const auto& [local,patch]:child_overrides.items())if(!local_ids.contains(local)||!patch.is_object())throw std::runtime_error("unknown prefab child override '"+local+"'");
    for(std::size_t i=0;i<templates.size();++i){
        Json item=templates[i];const std::string local=item.value("id",std::string("part_")+std::to_string(i));
        if(i==0)for(const auto& [key,value]:overrides.items())if(key!="id"&&key!="position"&&key!="entities")item[key]=value;
        if(child_overrides.contains(local))for(const auto& [key,value]:child_overrides[local].items())if(key!="id")item[key]=value;
        Entity entity=entity_from_json(item);
        if(!entity.parent.empty()){if(!local_ids.contains(entity.parent))throw std::runtime_error("unknown prefab parent '"+entity.parent+"'");entity.parent=local_ids.at(entity.parent);}
        if(i==0&&!entity.parent.empty())throw std::runtime_error("prefab first entity must be a root");
        if(overrides.contains("position")&&entity.parent.empty()){if(templates.size()==1)entity.position=offset;else for(int a=0;a<3;++a)entity.position[a]+=offset[a];}
        entity.id=i==0?instance:instance+"_"+local;entity.name=item.value("name",prefab);
        if(entity.id.size()>128||!ids.insert(entity.id).second)throw std::runtime_error("prefab local IDs must be unique and bounded");
        if(entity.role=="player")throw std::runtime_error("spawned prefabs cannot replace the authored player");
        validate_visual(entity);prepared.push_back(std::move(entity));
    }
    for(const auto& entity:prepared){std::set<std::string> chain;std::string parent=entity.parent;unsigned depth=0;
        while(!parent.empty()){if(++depth>16||!chain.insert(parent).second)throw std::runtime_error("prefab parent cycle or depth limit");
            const auto found=std::find_if(prepared.begin(),prepared.end(),[&](const auto& value){return value.id==parent;});
            if(found==prepared.end())throw std::runtime_error("prefab parent not found");parent=found->parent;}}
    for(auto& entity:prepared){
        std::size_t slot=0;while(slot<records.size()&&records[slot].alive)++slot;
        if(slot==records.size())records.emplace_back();
        auto& record=records[slot];record=Record{};record.source=std::move(entity);record.prefab=prefab;record.instance=instance;
        record.local_position=record.source.position;record.local_rotation=record.source.rotation;record.local_scale=record.source.visual_scale;
        record.ecs_id=ecs_new(ecs.get());record.pending_create=true;record.animation_start=elapsed;
        set_state(record,{record.source.position,record.source.color,record.source.visible,false});index.emplace(record.source.id,slot);
    }
    return instance;
}
static int script_spawn(lua_State* vm){try{auto& self=owner(vm);const std::string prefab=luaL_checkstring(vm,1);
    const auto id=self.spawn(prefab,lua_isnoneornil(vm,2)?Json::object():json_argument(vm,2));lua_pushlstring(vm,id.data(),id.size());return 1;
}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}
static int script_destroy(lua_State* vm){auto& self=owner(vm);const std::string id=luaL_checkstring(vm,1);const auto found=self.index.find(id);
    if(found==self.index.end()){lua_pushboolean(vm,false);return 1;}auto& record=self.records[found->second];
    const bool changed=!record.pending_destroy;record.pending_destroy=true;
    if(record.instance==id)for(auto& part:self.records)if(part.alive&&part.instance==id)part.pending_destroy=true;
    lua_pushboolean(vm,changed);return 1;}
static int script_exists(lua_State* vm){auto& self=owner(vm);const std::string id=luaL_checkstring(vm,1);const auto found=self.index.find(id);
    lua_pushboolean(vm,found!=self.index.end()&&self.records[found->second].alive&&!self.records[found->second].pending_destroy);return 1;}
static int script_asset(lua_State* vm){try{auto& self=owner(vm);auto& record=entity_argument(vm,1);Entity candidate=record.source;
    candidate.asset=luaL_checkstring(vm,2);candidate.animation=lua_isnoneornil(vm,3)?"":luaL_checkstring(vm,3);self.validate_visual(candidate);
    for(const auto& child:self.records)if(child.alive&&!child.pending_destroy&&child.source.parent==record.source.id&&!child.source.parent_joint.empty()){
        const auto model=self.compiled.models.find(candidate.asset);
        if(model==self.compiled.models.end()||std::none_of(model->second->joints.begin(),model->second->joints.end(),[&](const auto& joint){return joint.id==child.source.parent_joint;}))
            throw std::runtime_error("asset change would invalidate a joint attachment");}
    candidate.animation_previous.clear();record.source=candidate;record.animation_start=self.elapsed;record.blend_duration=0;
    return 0;}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}
static int script_scale(lua_State* vm){auto& record=entity_argument(vm,1);const Vec3 scale{number(vm,2),number(vm,3),number(vm,4)};
    for(float v:scale)if(v<.001f||v>1000)luaL_error(vm,"visual scale must be 0.001..1000");record.source.visual_scale=scale;record.local_scale=scale;return 0;}
static int script_opacity(lua_State* vm){auto& self=owner(vm);auto& record=entity_argument(vm,1);const float opacity=number(vm,2);
    if(opacity<0||opacity>1)luaL_error(vm,"opacity must be 0..1");auto value=self.state(record);value.color[3]=opacity;self.set_state(record,value);return 0;}
static int script_collision(lua_State* vm){auto& self=owner(vm);auto& record=entity_argument(vm,1);luaL_checktype(vm,2,LUA_TBOOLEAN);
    const bool enabled=lua_toboolean(vm,2);if(enabled==record.source.collision_enabled)return 0;
    const auto velocity=self.velocity(record);self.remove_body(record);record.source.collision_enabled=enabled;
    record.character_velocity=velocity;if(enabled&&!record.pending_create)self.create_body(record);self.set_velocity(record,velocity);return 0;}

static float action_value(const Input& input,const std::string& name){if(name=="move_x")return input.move_x;if(name=="move_z")return input.move_z;
    if(name=="jump")return input.jump?1.0f:0.0f;if(name=="fire")return input.fire?1.0f:0.0f;
    const auto found=input.actions.find(name);return found==input.actions.end()?0:found->second;}
static int script_pressed(lua_State* vm){auto& self=owner(vm);const std::string action=luaL_checkstring(vm,1);
    lua_pushboolean(vm,action_value(self.input,action)!=0&&action_value(self.previous_input,action)==0);return 1;}
static int script_released(lua_State* vm){auto& self=owner(vm);const std::string action=luaL_checkstring(vm,1);
    lua_pushboolean(vm,action_value(self.input,action)==0&&action_value(self.previous_input,action)!=0);return 1;}
static int script_events(lua_State* vm){BindingProbe probe(owner(vm),0);push_json(vm,owner(vm).last_input_events);return 1;}
static int script_ui_set(lua_State* vm){BindingProbe probe(owner(vm),1);try{auto& self=owner(vm);const auto key=key_argument(vm,1);auto value=json_argument(vm,2);
    auto candidate=self.ui_state;candidate[key]=value;if(candidate.dump().size()>1024*1024)throw std::runtime_error("UI state exceeds 1 MiB");
    self.ui_state=std::move(candidate);return 0;}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}
static int script_ui_get(lua_State* vm){const std::string key=luaL_checkstring(vm,1);const auto& state=owner(vm).ui_state;
    push_json(vm,state.contains(key)?state[key]:Json());return 1;}
static int script_ui_event(lua_State* vm){const std::string id=luaL_checkstring(vm,1);const std::string kind=lua_isnoneornil(vm,2)?"":luaL_checkstring(vm,2);Json matches=Json::array();
    for(const auto& event:owner(vm).last_input_events)if(event.is_object()&&event.value("id","")==id&&(kind.empty()||event.value("event","")==kind))matches.push_back(event);
    push_json(vm,matches);return 1;}
static int script_scene(lua_State* vm){auto& self=owner(vm);const std::string scene=luaL_checkstring(vm,1);
    if(!self.scene_catalog.contains(scene))luaL_error(vm,"unknown scene '%s'",scene.c_str());self.requested_scene=scene;return 0;}
static int script_scene_id(lua_State* vm){const auto& scene=owner(vm).project.scene_id;lua_pushlstring(vm,scene.data(),scene.size());return 1;}
static int session_request(lua_State* vm,const char* action){auto& self=owner(vm);const std::string slot=luaL_checkstring(vm,1);
    if(slot.empty()||slot.size()>64||!std::all_of(slot.begin(),slot.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}))luaL_error(vm,"save slot must use 1..64 ASCII letters, digits, '_' or '-'");
    if(self.session_requests.size()>=8)luaL_error(vm,"session request limit is 8 per tick");self.session_requests.push_back({{"action",action},{"slot",slot},{"tick",self.tick}});return 0;}
static int script_save_game(lua_State* vm){return session_request(vm,"save");}
static int script_load_game(lua_State* vm){return session_request(vm,"load");}
static int script_random(lua_State* vm){auto& self=owner(vm);auto x=self.random_state;x^=x>>12;x^=x<<25;x^=x>>27;self.random_state=x;
    const auto bits=(x*2685821657736338717ull)>>11;lua_pushnumber(vm,static_cast<double>(bits)*(1.0/9007199254740992.0));return 1;}
static int script_tick(lua_State* vm){lua_pushnumber(vm,static_cast<double>(owner(vm).tick));return 1;}
static int script_definitions(lua_State* vm){push_json(vm,owner(vm).project.definitions);return 1;}
static int script_entities(lua_State* vm){Json entities=Json::array();auto& self=owner(vm);const std::string role=lua_isnoneornil(vm,1)?"":luaL_checkstring(vm,1);
    for(const auto& record:self.records)if(record.alive&&!record.pending_destroy&&(role.empty()||role==record.source.role))entities.push_back(record.source.id);
    std::sort(entities.begin(),entities.end());push_json(vm,entities);return 1;}
static int script_grounded(lua_State* vm){auto& self=owner(vm);lua_pushboolean(vm,self.grounded(entity_argument(vm,1)));return 1;}

bool query_accepts(const Record& record,const Json& options)const {
    if(!has_body(record))return false;
    if(record.source.body=="trigger"&&!options.value("sensors",true))return false;
    if(options.contains("role")&&options["role"]!=record.source.role)return false;
    if(options.contains("exclude"))for(const auto& id:options["exclude"])if(id==record.source.id)return false;
    return true;
}
static std::size_t query_limit(const Json& options){const int limit=options.value("limit",64);if(limit<1||limit>256)throw std::runtime_error("query limit must be 1..256");return static_cast<std::size_t>(limit);}
Json query_ray(Vec3 from,Vec3 to,const Json& options)const {
    if(!finite_vec(from)||!finite_vec(to)||!options.is_object())throw std::runtime_error("raycast requires finite endpoints and options");
    const auto limit=query_limit(options);
    const float radius=options.value("radius",0.0f);
    if(!std::isfinite(radius)||radius<0||radius>10000)throw std::runtime_error("query radius must be 0..10000");
    Vec3 delta{to[0]-from[0],to[1]-from[1],to[2]-from[2]};if(project.dimension=="2d")delta[2]=0;
    const float length=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
    Json hits=Json::array();
    if(length<1e-6f)return {{"hits",hits},{"truncated",false}};
    if(project.dimension=="2d"){
        struct Context{const Impl* world;const Json* options;Json* hits;float length;float z;} context{this,&options,&hits,length,from[2]};
        auto collect=[](b2ShapeId shape,b2Vec2 point,b2Vec2 normal,float fraction,void* opaque)->float{
            auto& c=*static_cast<Context*>(opaque);const auto body=b2Shape_GetBody(shape);
            for(const auto& record:c.world->records)if(B2_ID_EQUALS(record.box_body,body)&&c.world->query_accepts(record,*c.options)){
                c.hits->push_back({{"id",record.source.id},{"fraction",fraction},{"distance",fraction*c.length},
                                  {"point",{point.x,point.y,c.z}},{"normal",{normal.x,normal.y,0}}});break;}
            return 1.0f;
        };
        if(radius==0)b2World_CastRay(box.id,{from[0],from[1]},{delta[0],delta[1]},b2DefaultQueryFilter(),collect,&context);
        else{b2Vec2 center{from[0],from[1]};b2ShapeProxy proxy=b2MakeProxy(&center,1,radius);
            b2World_CastShape(box.id,&proxy,{delta[0],delta[1]},b2DefaultQueryFilter(),collect,&context);}
    }else if(radius==0){
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        JPH::RayCastSettings settings;
        physics->GetNarrowPhaseQuery().CastRay(JPH::RRayCast(JPH::RVec3(from[0],from[1],from[2]),JPH::Vec3(delta[0],delta[1],delta[2])),settings,collector);
        for(const auto& hit:collector.mHits)for(const auto& record:records)if(record.jolt_body==hit.mBodyID&&query_accepts(record,options)){
            const Vec3 point{from[0]+delta[0]*hit.mFraction,from[1]+delta[1]*hit.mFraction,from[2]+delta[2]*hit.mFraction};
            Vec3 normal{0,0,0};JPH::BodyLockRead lock(physics->GetBodyLockInterface(),hit.mBodyID);
            if(lock.Succeeded()){const auto n=lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,JPH::RVec3(point[0],point[1],point[2]));normal={n.GetX(),n.GetY(),n.GetZ()};}
            hits.push_back({{"id",record.source.id},{"fraction",hit.mFraction},{"distance",hit.mFraction*length},{"point",point},{"normal",normal}});break;
        }
    }else{
        JPH::SphereShape sphere(radius);
        JPH::RShapeCast cast(&sphere,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(JPH::RVec3(from[0],from[1],from[2])),JPH::Vec3(delta[0],delta[1],delta[2]));
        JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;JPH::ShapeCastSettings settings;
        physics->GetNarrowPhaseQuery().CastShape(cast,settings,JPH::RVec3::sZero(),collector);
        for(const auto& hit:collector.mHits)for(const auto& record:records)if(record.jolt_body==hit.mBodyID2&&query_accepts(record,options)){
            const auto n=-hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
            hits.push_back({{"id",record.source.id},{"fraction",hit.mFraction},{"distance",hit.mFraction*length},
                {"point",{from[0]+delta[0]*hit.mFraction,from[1]+delta[1]*hit.mFraction,from[2]+delta[2]*hit.mFraction}},
                {"normal",{n.GetX(),n.GetY(),n.GetZ()}}});break;
        }
    }
    std::sort(hits.begin(),hits.end(),[](const Json& a,const Json& b){return a["fraction"]==b["fraction"]?a["id"]<b["id"]:a["fraction"]<b["fraction"];});
    Json unique=Json::array();std::set<std::string> seen;
    for(const auto& hit:hits)if(seen.insert(hit["id"].get<std::string>()).second)unique.push_back(hit);
    const bool truncated=unique.size()>limit;if(truncated)unique.erase(unique.begin()+static_cast<std::ptrdiff_t>(limit),unique.end());
    return {{"hits",unique},{"truncated",truncated}};
}
Json query_overlap(Vec3 center,float radius,const Json& options)const {
    if(!finite_vec(center)||!std::isfinite(radius)||radius<=0||radius>10000||!options.is_object())throw std::runtime_error("overlap requires finite center and radius in (0,10000]");
    const auto limit=query_limit(options);std::set<std::string> ids;
    if(project.dimension=="2d"){
        struct Context{const Impl* world;const Json* options;std::set<std::string>* ids;}context{this,&options,&ids};
        b2Vec2 point{center[0],center[1]};const auto shape=b2MakeProxy(&point,1,radius);
        b2World_OverlapShape(box.id,&shape,b2DefaultQueryFilter(),[](b2ShapeId shape,void* opaque)->bool{
            auto& c=*static_cast<Context*>(opaque);const auto body=b2Shape_GetBody(shape);
            for(const auto& record:c.world->records)if(B2_ID_EQUALS(record.box_body,body)&&c.world->query_accepts(record,*c.options))c.ids->insert(record.source.id);
            return true;},&context);
    }else{
        JPH::SphereShape sphere(radius);JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;JPH::CollideShapeSettings settings;
        physics->GetNarrowPhaseQuery().CollideShape(&sphere,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(JPH::RVec3(center[0],center[1],center[2])),
                                                  settings,JPH::RVec3::sZero(),collector);
        for(const auto& hit:collector.mHits)for(const auto& record:records)if(record.jolt_body==hit.mBodyID2&&query_accepts(record,options))ids.insert(record.source.id);
    }
    Json result=Json::array();for(const auto& id:ids){if(result.size()==limit)break;result.push_back(id);}
    return {{"entities",result},{"truncated",ids.size()>limit}};
}
static int script_raycast(lua_State* vm){try{auto& self=owner(vm);const Vec3 from{number(vm,1),number(vm,2),number(vm,3)},to{number(vm,4),number(vm,5),number(vm,6)};
    push_json(vm,self.query_ray(from,to,lua_isnoneornil(vm,7)?Json::object():json_argument(vm,7)));return 1;
}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}
static int script_overlap(lua_State* vm){try{auto& self=owner(vm);const Vec3 center{number(vm,1),number(vm,2),number(vm,3)};
    push_json(vm,self.query_overlap(center,number(vm,4),lua_isnoneornil(vm,5)?Json::object():json_argument(vm,5)));return 1;
}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}

void update_characters(float dt){
    for(auto& record:records)if(record.alive&&!record.pending_destroy&&!record.pending_create&&(record.source.body=="dynamic"||record.source.body=="kinematic")&&!record.source.collision_enabled){
        auto current=state(record);for(int axis=0;axis<(project.dimension=="2d"?2:3);++axis){if(record.source.body=="dynamic")record.character_velocity[axis]+=project.gravity[axis]*dt;current.position[axis]+=record.character_velocity[axis]*dt;}
        set_state(record,current);if(record.source.controller=="character")record.movement_velocity=record.character_velocity;
    }
    if(project.dimension=="2d"){
        for(auto& record:records){if(!has_body(record)||record.source.controller!="character")continue;
            auto current=state(record);auto velocity=record.movement_velocity;velocity[0]+=project.gravity[0]*dt;velocity[1]+=project.gravity[1]*dt;
            const float radius=record.source.size[0]*.5f,half=std::max(0.0f,record.source.size[1]*.5f-radius);
            Vec3 carry{0,0,0};const Vec3 old_foot{current.position[0],current.position[1]-half,0};
            const auto previous_support=query_ray(old_foot,{old_foot[0],old_foot[1]-radius-.08f,0},{{"exclude",{record.source.id}},{"limit",1},{"sensors",false}});
            if(record.character_grounded&&!previous_support["hits"].empty()&&previous_support["hits"][0]["normal"][1].get<float>()>=.707f){
                const auto support_id=previous_support["hits"][0]["id"].get<std::string>();carry=this->velocity(records[index.at(support_id)]);velocity[0]+=carry[0];velocity[1]+=carry[1];}
            const b2QueryFilter filter{2,~std::uint64_t{6}};
            b2Vec2 position{current.position[0],current.position[1]},target{position.x+velocity[0]*dt,position.y+velocity[1]*dt};
            std::array<b2CollisionPlane,64> planes{};int count=0;
            struct Context{b2CollisionPlane* planes;int* count;};Context context{planes.data(),&count};
            for(int iteration=0;iteration<5;++iteration){count=0;b2Capsule mover{{position.x,position.y-half},{position.x,position.y+half},radius};
                b2World_CollideMover(box.id,&mover,filter,[](b2ShapeId,const b2PlaneResult* plane,void* opaque)->bool{
                    auto& c=*static_cast<Context*>(opaque);if(plane->hit&&*c.count<64)c.planes[(*c.count)++]={plane->plane,std::numeric_limits<float>::max(),0,true};return *c.count<64;},&context);
                const auto solved=b2SolvePlanes({target.x-position.x,target.y-position.y},planes.data(),count);
                const float fraction=b2World_CastMover(box.id,&mover,solved.translation,filter);
                const b2Vec2 delta{solved.translation.x*fraction,solved.translation.y*fraction};position.x+=delta.x;position.y+=delta.y;
                if(delta.x*delta.x+delta.y*delta.y<.000001f)break;
            }
            const auto clipped=b2ClipVector({velocity[0],velocity[1]},planes.data(),count);record.character_velocity={clipped.x,clipped.y,0};
            current.position[0]=position.x;current.position[1]=position.y;set_state(record,current);
            b2Body_SetTransform(record.box_body,position,b2Rot_identity);
            const Vec3 foot{position.x,position.y-half,0};const auto support=query_ray(foot,{foot[0],foot[1]-radius-.08f,0},{{"exclude",{record.source.id}},{"limit",1},{"sensors",false}});
            record.character_grounded=clipped.y-carry[1]<=.1f&&!support["hits"].empty()&&support["hits"][0]["normal"][1].get<float>()>=.707f;
            record.movement_velocity={clipped.x-carry[0],clipped.y-carry[1],0};
            if(record.character_grounded&&record.movement_velocity[1]<0)record.movement_velocity[1]=0;
        }
        return;
    }
    if(!physics)return;
    for(auto& record:records){if(!has_body(record)||!record.character)continue;auto& c=*record.character;
        c.UpdateGroundVelocity();auto velocity=record.movement_velocity;JPH::Vec3 carry=JPH::Vec3::sZero();
        if(c.IsSupported()){carry=c.GetGroundVelocity();if(velocity[1]<=0)velocity[1]=0;}else velocity[1]+=project.gravity[1]*dt;
        velocity[0]+=carry.GetX();velocity[1]+=carry.GetY();velocity[2]+=carry.GetZ();
        c.SetLinearVelocity(JPH::Vec3(velocity[0],velocity[1],velocity[2]));
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        c.ExtendedUpdate(dt,JPH::Vec3(project.gravity[0],project.gravity[1],project.gravity[2]),settings,
            physics->GetDefaultBroadPhaseLayerFilter(dynamic_layer),physics->GetDefaultLayerFilter(dynamic_layer),{},{},*temporary);
        const auto result=c.GetLinearVelocity();record.character_velocity={result.GetX(),result.GetY(),result.GetZ()};
        const auto desired=c.IsSupported()?result-carry:result;record.movement_velocity={desired.GetX(),desired.GetY(),desired.GetZ()};
        if(c.IsSupported()&&record.movement_velocity[1]<0)record.movement_velocity[1]=0;
    }
}
void update_camera(float dt){
    if(!camera.value("enabled",false))return;
    if(camera.value("mode",std::string("fixed"))=="fixed"){camera["target_locked"]=false;camera["occluded"]=false;return;}
    const std::string follow=camera.value("follow",std::string());
    if(follow.empty()||!index.contains(follow))return;
    const auto& record=records[index.at(follow)];if(!record.alive||record.pending_destroy)return;
    Vec3 target=state(record).position;const Vec3 origin=target;const Vec3 offset=camera.value("offset",Vec3{0,5,8});Vec3 desired=target;
    for(int i=0;i<3;++i)desired[i]+=offset[i];
    const std::string target_entity=camera.value("target_entity",std::string());
    const bool target_valid=!target_entity.empty()&&index.contains(target_entity)&&records[index.at(target_entity)].alive&&!records[index.at(target_entity)].pending_destroy;
    camera["target_locked"]=target_valid;
    if(target_valid){
        const auto other=state(records[index.at(target_entity)]).position;
        if(camera.value("mode",std::string())=="lock_on"){
            for(int axis=0;axis<3;++axis)target[axis]=(origin[axis]+other[axis])*.5f;
            const float dx=other[0]-origin[0],dz=other[2]-origin[2],length=std::sqrt(dx*dx+dz*dz);
            if(length>.001f){const float distance=std::max(1.0f,std::sqrt(offset[0]*offset[0]+offset[2]*offset[2]));
                desired={origin[0]-dx/length*distance,origin[1]+offset[1],origin[2]-dz/length*distance};}
        }else target=other;
    }
    camera["occluded"]=false;
    if(camera.value("obstruction",true)){
        Json excluded=Json::array({follow});if(target_valid)excluded.push_back(target_entity);
        auto hits=query_ray(origin,desired,{{"radius",.2f},{"exclude",excluded},{"limit",1},{"sensors",false}});
        if(!hits["hits"].empty()){const float fraction=std::max(.02f,hits["hits"][0]["fraction"].get<float>()-.03f);
            camera["occluded"]=true;for(int i=0;i<3;++i)desired[i]=origin[i]+(desired[i]-origin[i])*fraction;}
    }
    Vec3 position=camera.value("position",desired);const float smoothing=camera.value("smoothing",8.0f);
    const float fraction=smoothing<=0?1.0f:1.0f-std::exp(-smoothing*dt);
    for(int i=0;i<3;++i)position[i]+=(desired[i]-position[i])*fraction;
    if(camera.value("obstruction",true)){
        Json excluded=Json::array({follow});if(target_valid)excluded.push_back(target_entity);
        const auto hits=query_ray(origin,position,{{"radius",.2f},{"exclude",excluded},{"limit",1},{"sensors",false}});
        if(!hits["hits"].empty()){const float safe=std::max(.001f,hits["hits"][0]["fraction"].get<float>()-.01f);camera["occluded"]=true;
            for(int axis=0;axis<3;++axis)position[axis]=origin[axis]+(position[axis]-origin[axis])*safe;}
    }
    float distance_squared=0;for(int axis=0;axis<3;++axis)distance_squared+=(position[axis]-target[axis])*(position[axis]-target[axis]);
    if(distance_squared<.000001f)position[2]+=1;
    camera["position"]=position;camera["target"]=target;
}
static int script_camera(lua_State* vm){try{auto& self=owner(vm);const auto changes=json_argument(vm,1);if(!changes.is_object())throw std::runtime_error("camera_set expects an object");
    auto candidate=self.camera;for(const auto& [key,value]:changes.items())candidate[key]=value;candidate["enabled"]=true;
    validate_camera(candidate);
    self.camera=std::move(candidate);return 0;}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}
static int script_camera_get(lua_State* vm){push_json(vm,owner(vm).camera);return 1;}
static void validate_camera(const Json& candidate){
    if(!candidate.is_object())throw std::runtime_error("camera must be an object");
    for(const auto* key:{"position","target","offset"}){
        const auto vector=candidate.at(key).get<Vec3>();if(!finite_vec(vector))throw std::runtime_error("camera vector must be finite");
        for(float value:vector)if(std::abs(value)>100000)throw std::runtime_error("camera vector exceeds world bounds");
    }
    if(candidate.at("position")==candidate.at("target"))throw std::runtime_error("camera position and target must differ");
    const auto mode=candidate.value("mode",std::string("fixed"));if(mode!="fixed"&&mode!="follow"&&mode!="third_person"&&mode!="orthographic"&&mode!="lock_on")throw std::runtime_error("unknown camera mode");
    const float smoothing=candidate.value("smoothing",8.0f);if(!std::isfinite(smoothing)||smoothing<0||smoothing>100)throw std::runtime_error("camera smoothing must be 0..100");
    const float fov=candidate.value("fov",60.0f),height=candidate.value("half_height",11.0f);
    if(!std::isfinite(fov)||fov<10||fov>150||!std::isfinite(height)||height<.01f||height>10000)throw std::runtime_error("camera fov must be 10..150 and half_height 0.01..10000");
    (void)candidate.value("enabled",false);(void)candidate.value("obstruction",true);(void)candidate.value("follow",std::string());(void)candidate.value("target_entity",std::string());
}

Json nav_path(Vec3 from,Vec3 to)const{if(!navigation)throw std::runtime_error("project has no navigation mesh");return navigation->path(from,to);}
static int script_nav_path(lua_State* vm){try{auto& self=owner(vm);const Vec3 from{number(vm,1),number(vm,2),number(vm,3)},to{number(vm,4),number(vm,5),number(vm,6)};
    push_json(vm,self.nav_path(from,to));return 1;}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}
static int script_navigate(lua_State* vm){try{auto& self=owner(vm);auto& record=entity_argument(vm,1);const Vec3 goal{number(vm,2),number(vm,3),number(vm,4)};
    const float speed=lua_isnoneornil(vm,5)?3.0f:number(vm,5);if(speed<=0||speed>100)throw std::runtime_error("navigation speed must be (0,100]");
    if(!self.nav_agents.contains(record.source.id)&&self.nav_agents.size()>=256)throw std::runtime_error("navigation supports at most 256 active agents");
    auto path=self.nav_path(self.state(record).position,goal);path["goal"]=goal;path["speed"]=speed;path["waypoint"]=1;
    self.nav_agents[record.source.id]=path;push_json(vm,path);return 1;}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}
static int script_nav_stop(lua_State* vm){auto& self=owner(vm);auto& record=entity_argument(vm,1);self.nav_agents.erase(record.source.id);self.set_velocity(record,{0,0,0});return 0;}
static int script_nav_status(lua_State* vm){auto& self=owner(vm);const std::string id=luaL_checkstring(vm,1);
    push_json(vm,self.nav_agents.contains(id)?self.nav_agents.at(id):Json{{"status","idle"}});return 1;}
static int script_nav_rebuild(lua_State* vm){try{auto& self=owner(vm);std::vector<Entity> geometry;
    for(const auto& record:self.records)if(record.alive&&!record.pending_destroy){auto entity=record.source;entity.position=self.state(record).position;geometry.push_back(entity);}
    auto next=std::make_unique<Navigation>(self.project.dimension,geometry,self.project.document.value("navigation",Json::object()));self.navigation=std::move(next);
    for(auto& [id,agent]:self.nav_agents)if(self.index.contains(id)){auto path=self.nav_path(self.state(self.records[self.index.at(id)]).position,agent["goal"].get<Vec3>());
        agent["status"]=path["status"];agent["points"]=path["points"];agent["waypoint"]=1;}
    return 0;}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}
void update_navigation(float dt){
    if(!navigation)return;Json orders=Json::object();
    for(const auto& [id,agent]:nav_agents)if(index.contains(id)&&records[index.at(id)].alive&&!records[index.at(id)].pending_destroy&&agent.value("status","")!="unreachable")
        orders[id]={{"position",state(records[index.at(id)]).position},{"goal",agent["goal"]},{"speed",agent["speed"]}};
    const auto updates=navigation->steer(orders,dt);
    for(const auto& [id,update]:updates.items())if(index.contains(id)){
        auto& agent=nav_agents[id];agent["status"]=update["status"];agent["neighbors"]=update.value("neighbors",0);agent["partial"]=update.value("partial",false);
        auto& record=records[index.at(id)];auto position=state(record).position;auto velocity=update["velocity"].get<Vec3>();
        if(record.source.body=="dynamic"){if(project.dimension=="3d")velocity[1]=this->velocity(record)[1];set_velocity(record,velocity);}
        else{for(int axis=0;axis<3;++axis)position[axis]+=velocity[axis]*dt;teleport(record,position);}
    }
}
static int script_bus(lua_State* vm){auto& self=owner(vm);const std::string bus=luaL_checkstring(vm,1);const float gain=number(vm,2),pan=number(vm,3);luaL_checktype(vm,4,LUA_TBOOLEAN);
    if(!self.buses.contains(bus))luaL_error(vm,"unknown audio bus '%s'",bus.c_str());if(gain<0||gain>2||pan<-1||pan>1)luaL_error(vm,"bus gain must be 0..2 and pan -1..1");
    const bool mute=lua_toboolean(vm,4);self.buses[bus]["gain"]=gain;self.buses[bus]["pan"]=pan;self.buses[bus]["mute"]=mute;
    if(++self.audio_count>32)luaL_error(vm,"audio event limit is 32 per update");
    self.sounds.push_back({{"serial",++self.audio_serial},{"tick",self.tick},{"scene",self.project.scene_id},{"action","bus"},{"bus",bus},{"gain",gain},{"pan",pan},{"mute",mute}});
    if(self.sounds.size()>128)self.sounds.erase(self.sounds.begin());return 0;}

Json save_state()const {
    Json entities=Json::array();
    for(const auto& record:records){if(!record.alive||record.pending_destroy)continue;auto entity=record.source;const auto current=state(record);
        entity.position=record.source.parent.empty()?current.position:record.local_position;entity.color=current.color;entity.visible=current.visible;
        if(!record.source.parent.empty()){entity.rotation=record.local_rotation;entity.visual_scale=record.local_scale;}
        auto saved=entity_json(entity);saved["angular_velocity"]=record.spin;saved["velocity"]=velocity(record);saved["movement_velocity"]=record.movement_velocity;saved["collected"]=current.collected;saved["prefab"]=record.prefab;saved["instance"]=record.instance;
        saved["animation_start"]=record.animation_start;saved["previous_animation_start"]=record.previous_animation_start;
        saved["animation_previous"]=entity.animation_previous;saved["blend_start"]=record.blend_start;saved["blend_duration"]=record.blend_duration;
        entities.push_back(std::move(saved));
    }
    auto compatibility=project.document.is_object()?project.document.value("save_compatibility",Json::object()):Json::object();
    compatibility["structural_fingerprint"]=save_structure_fingerprint(project);
    return {{"format_version",1},{"source_revision",project.revision},{"scene",project.scene_id},{"dimension",project.dimension},
            {"save_compatibility",compatibility},{"profile",project.profile},
            {"project_id",project.document.is_object()?project.document.value("project_id",project.title):project.title},
            {"game_save_version",project.document.is_object()?project.document.value("save_version",1):1},
            {"tick",tick},{"time",elapsed},{"spawn_serial",spawn_serial},{"random_state",random_state},{"entities",entities},
            {"state",game_state},{"ui",ui_state},{"camera",camera},{"audio_buses",buses},{"music_layers",music_layers},{"music",current_music},{"music_bus",current_music_bus},{"music_loop",current_music_loop},
            {"navigation_agents",nav_agents},{"contacts",contact_pairs},{"won",did_win},{"collected",collection_count},{"message",status_message},
            {"input",{{"move_x",input.move_x},{"move_z",input.move_z},{"jump",input.jump},{"fire",input.fire},{"actions",input.actions},{"pointer",input.pointer}}}};
}
static void validate_saved_state(const Json& saved,const Project& project){
    if(!saved.is_object()||saved.value("format_version",0)!=1)throw std::runtime_error("Unsupported player save format");
    if(saved.value("source_revision",std::string())!=project.revision)throw std::runtime_error("Save source revision differs; explicit migration is required");
    const std::string project_id=project.document.is_object()?project.document.value("project_id",project.title):project.title;
    if(saved.value("project_id",std::string())!=project_id)throw std::runtime_error("Save belongs to another project");
    if(saved.value("game_save_version",0)!=(project.document.is_object()?project.document.value("save_version",1):1))throw std::runtime_error("Save game version differs; explicit migration is required");
    if(saved.value("dimension",std::string())!=project.dimension)throw std::runtime_error("Save dimension differs from the project");
    if(!saved.contains("entities")||!saved["entities"].is_array()||saved["entities"].size()>4096)throw std::runtime_error("Save entities must be an array with at most 4096 entries");
    if(!saved.contains("state")||!saved["state"].is_object()||saved["state"].dump().size()>8*1024*1024)throw std::runtime_error("Saved game state must be an object up to 8 MiB");
    validate_data(saved["state"]);validate_data(saved.value("ui",Json::object()));
    if(saved.dump().size()>16*1024*1024)throw std::runtime_error("Save exceeds 16 MiB");
    const double time=saved.value("time",0.0);if(!std::isfinite(time)||time<0)throw std::runtime_error("Save time must be finite and nonnegative");
    for(const auto* key:{"tick","spawn_serial","random_state"})if(!saved.contains(key)||!saved[key].is_number_unsigned())throw std::runtime_error(std::string("Save ")+key+" must be an unsigned integer");
    validate_camera(saved.at("camera"));
    if(!saved.value("ui",Json::object()).is_object()||saved.value("ui",Json::object()).dump().size()>1024*1024)throw std::runtime_error("Saved UI state must be an object up to 1 MiB");
    if(!saved.at("audio_buses").is_object()||saved["audio_buses"].size()>8)throw std::runtime_error("Saved audio buses are invalid");
    for(const auto& [id,bus]:saved["audio_buses"].items()){
        if(id.empty()||id.size()>64||!bus.is_object())throw std::runtime_error("Saved audio bus is invalid");
        const float gain=bus.value("gain",1.0f),pan=bus.value("pan",0.0f);(void)bus.value("mute",false);
        if(!std::isfinite(gain)||gain<0||gain>2||!std::isfinite(pan)||pan<-1||pan>1)throw std::runtime_error("Saved audio bus parameters are invalid");
    }
    if(!saved.value("navigation_agents",Json::object()).is_object())throw std::runtime_error("Saved navigation agents must be an object");
}
Json migrate_save(const Json& saved,int from,int to){
    validate_data(saved);
    auto* vm=script.get();migration_source=&saved;migration_from=from;migration_to=to;
    lua_pushcfunction(vm,script_migration_entry,"migrate_save");
    try{protected_call("migrate",0,1);}catch(...){migration_source=nullptr;throw;}
    migration_source=nullptr;
    // Whole saves contain up to 4096 entity records, unlike a single state value.
    // Keep ordinary script-state limits unchanged and validate the returned save below.
    auto result=json_argument(vm,-1,16*1024*1024,1024*1024);lua_pop(vm,1);
    if(!result.is_object())throw std::runtime_error("migrate must return a save object");return result;
}
static int script_migration_entry(lua_State* vm){auto& self=owner(vm);lua_getref(vm,self.script_table);lua_rawgetfield(vm,-1,"migrate");lua_remove(vm,-2);
    if(!lua_isfunction(vm,-1))luaL_error(vm,"Save content changed; provide migrate(saved, from_version, to_version) returning updated save data");
    // Decode bounded, validated native save data inside the protected Lua call
    // for allocation safety, but start the authored hook's clock after decoding.
    // push_json creates plain data tables and cannot invoke game code.
    self.executing_script=false;
    push_json(vm,*self.migration_source);lua_pushinteger(vm,self.migration_from);lua_pushinteger(vm,self.migration_to);
    self.begin_script_budget(false);lua_call(vm,3,1);return 1;
}
static int script_viewport(lua_State* vm){push_json(vm,owner(vm).viewport_state);return 1;}
static std::unique_ptr<Impl> from_save(Project project,const Json& saved,const Json& viewport,bool profiling=false){
    validate_saved_state(saved,project);
    project.save_fingerprint=save_structure_fingerprint(project);
    project.entities.clear();std::set<std::string> ids;
    for(const auto& entry:saved["entities"]){auto entity=entity_from_json(entry);
        if(entity.id.empty()||entity.id.size()>128||!ids.insert(entity.id).second)throw std::runtime_error("Saved entity IDs are empty, duplicated or too long");
        project.entities.push_back(std::move(entity));}
    auto result=std::make_unique<Impl>(project,false);
    result->profile_bindings=profiling;
    result->tick=saved.at("tick").get<std::uint64_t>();result->elapsed=saved.at("time").get<double>();
    result->spawn_serial=saved.at("spawn_serial").get<std::uint64_t>();result->random_state=saved.at("random_state").get<std::uint64_t>();
    result->game_state=saved.at("state");result->ui_state=saved.value("ui",Json::object());result->camera=saved.at("camera");
    for(const auto& [bus,config]:saved["audio_buses"].items())if(!result->buses.contains(bus))throw std::runtime_error("Save references unknown audio bus");
    for(const auto* bus:{"master","music","sfx"})if(!saved["audio_buses"].contains(bus))throw std::runtime_error("Save is missing a required audio bus");
    result->buses=saved.at("audio_buses");result->current_music=saved.value("music",std::string());
    result->current_music_bus=saved.value("music_bus",std::string("music"));result->current_music_loop=saved.value("music_loop",true);
    if(!result->buses.contains(result->current_music_bus))throw std::runtime_error("Saved music references unknown audio bus");
    result->nav_agents=saved.value("navigation_agents",std::map<std::string,Json>{});
    const auto saved_contacts=saved.value("contacts",Json::array());
    if(!saved_contacts.is_array()||saved_contacts.size()>32768)throw std::runtime_error("Saved contacts are invalid");
    for(const auto& pair:saved_contacts){const auto ids=pair.get<std::array<std::string,2>>();
        if(!result->index.contains(ids[0])||!result->index.contains(ids[1]))throw std::runtime_error("Saved contact references missing entity");
        result->contact_pairs.emplace(ids[0],ids[1]);}
    if(result->nav_agents.size()>256)throw std::runtime_error("Save has more than 256 navigation agents");
    for(const auto& [id,agent]:result->nav_agents){
        if(!result->index.contains(id)||!agent.is_object())throw std::runtime_error("Save navigation references unknown entity");
        const auto goal=agent.at("goal").get<Vec3>();const float speed=agent.at("speed").get<float>();
        if(!finite_vec(goal)||!std::isfinite(speed)||speed<=0||speed>100)throw std::runtime_error("Saved navigation goal/speed are invalid");
        const auto status=agent.value("status",std::string());if(status!="ready"&&status!="arrived"&&status!="unreachable")throw std::runtime_error("Saved navigation status is invalid");
    }
    result->did_win=saved.value("won",false);result->collection_count=saved.value("collected",0);result->status_message=saved.value("message",std::string());
    // Physical input is transient. Never resurrect held pointers/actions, nor
    // manufacture an input_released edge on the first restored update.
    result->input=Input{};result->previous_input=Input{};result->viewport_state=viewport;
    if(!result->current_music.empty()&&!result->compiled.music.contains(result->current_music))throw std::runtime_error("Save references unknown music");
    result->music_layers=saved.value("music_layers",Json::object());
    if(!result->music_layers.is_object()||result->music_layers.size()>16)throw std::runtime_error("Invalid saved music layers");
    for(auto&[id,layer]:result->music_layers.items()) {
        if(result->current_music.empty()||!music_has_layer(*result->compiled.music.at(result->current_music),id)||!layer.is_object()||!layer.contains("gain")||!layer["gain"].is_number())throw std::runtime_error("Invalid saved music layer");
        double gain=layer["gain"].get<double>();if(!std::isfinite(gain)||gain<0||gain>2)throw std::runtime_error("Invalid saved music layer gain");
    }

    for(const auto& entry:saved["entities"]){auto& record=result->records[result->index.at(entry["id"].get<std::string>())];
        record.spin=entry.value("angular_velocity",0.0f);if(!std::isfinite(record.spin)||std::abs(record.spin)>3600)throw std::runtime_error("Invalid saved angular velocity");
        if(record.source.physics2d.value("rotate",false)&&result->has_body(record))b2Body_SetAngularVelocity(record.box_body,record.spin*3.14159265f/180);
        const auto motion=entry.at("velocity").get<Vec3>();if(!finite_vec(motion))throw std::runtime_error("Saved velocity must be finite");result->set_velocity(record,motion);
        record.movement_velocity=entry.value("movement_velocity",motion);if(!finite_vec(record.movement_velocity))throw std::runtime_error("Saved movement velocity must be finite");
        auto current=result->state(record);current.collected=entry.value("collected",false);result->set_state(record,current);
        record.prefab=entry.value("prefab",std::string());record.instance=entry.value("instance",std::string());record.animation_start=entry.value("animation_start",0.0);
        record.previous_animation_start=entry.value("previous_animation_start",0.0);record.source.animation_previous=entry.value("animation_previous",std::string());
        record.blend_start=entry.value("blend_start",0.0);record.blend_duration=entry.value("blend_duration",0.0);
        if(!std::isfinite(record.animation_start)||!std::isfinite(record.previous_animation_start)||!std::isfinite(record.blend_start)||
           !std::isfinite(record.blend_duration)||record.blend_duration<0)throw std::runtime_error("Saved animation times must be finite");
    }
    result->call_hook("on_load",false,0);result->flush_entities();return result;
}

void update_animation_events(float dt){
    for(auto& record:records){record.animation_events=Json::array();if(!record.alive||record.pending_destroy)continue;
        const auto found=compiled.models.find(record.source.asset);if(found==compiled.models.end())continue;
        const double from=(elapsed-record.animation_start)*record.source.animation_speed;
        record.animation_events=model_markers(*found->second,record.source.animation,from,from+dt*record.source.animation_speed);
    }
}
static int script_animation_events(lua_State* vm){push_json(vm,entity_argument(vm,1).animation_events);return 1;}
static int script_joint_position(lua_State* vm){try{auto& self=owner(vm);const auto& record=entity_argument(vm,1);const std::string joint=luaL_checkstring(vm,2);
    const auto found=self.compiled.models.find(record.source.asset);if(found==self.compiled.models.end())throw std::runtime_error("joint_position requires an animated model");
    const auto& model=*found->second;const float blend=record.blend_duration<=0?1.0f:static_cast<float>(std::clamp((self.elapsed-record.blend_start)/record.blend_duration,0.0,1.0));
    const auto pose=sample_model_pose(model,record.source.animation,(self.elapsed-record.animation_start)*record.source.animation_speed,
        record.source.animation_previous,(self.elapsed-record.previous_animation_start)*record.source.animation_speed,blend);
    const auto matrix=model_joint_transform(model,pose,joint);Vec3 local{matrix[12],matrix[13],matrix[14]};
    for(int axis=0;axis<3;++axis)local[axis]*=record.source.size[axis]*record.source.visual_scale[axis];
    const auto rotation=record.source.rotation;constexpr float radians=3.14159265359f/180.0f;
    for(int axis=0;axis<3;++axis){const int a=(axis+1)%3,b=(axis+2)%3;const float c=std::cos(rotation[axis]*radians),s=std::sin(rotation[axis]*radians);const float x=local[a],y=local[b];local[a]=c*x-s*y;local[b]=s*x+c*y;}
    const auto position=self.state(record).position;for(int axis=0;axis<3;++axis)lua_pushnumber(vm,local[axis]+position[axis]);return 3;
}catch(const std::exception& error){luaL_error(vm,"%s",error.what());}return 0;}

void record_contact(JPH::BodyID a,JPH::BodyID b){
    std::string first,second;for(const auto& record:records)if(record.alive&&!record.pending_destroy){if(record.jolt_body==a)first=record.source.id;if(record.jolt_body==b)second=record.source.id;}
    if(first.empty()||second.empty())return;if(first>second)std::swap(first,second);contact_candidates.emplace(first,second);
}
void update_contacts(){
    if(project.dimension=="2d"){
        contact_candidates=contact_pairs;const auto events=b2World_GetContactEvents(box.id);
        auto identity=[&](b2ShapeId a,b2ShapeId b){std::string first,second;
            if(!b2Shape_IsValid(a)||!b2Shape_IsValid(b))return std::pair{first,second};
            const auto body_a=b2Shape_GetBody(a),body_b=b2Shape_GetBody(b);
            for(const auto& record:records)if(record.alive&&!record.pending_destroy){if(B2_ID_EQUALS(record.box_body,body_a))first=record.source.id;if(B2_ID_EQUALS(record.box_body,body_b))second=record.source.id;}
            if(first>second)std::swap(first,second);return std::pair{first,second};};
        for(int i=0;i<events.beginCount;++i){const auto pair=identity(events.beginEvents[i].shapeIdA,events.beginEvents[i].shapeIdB);if(!pair.first.empty()&&!pair.second.empty())contact_candidates.insert(pair);}
        for(int i=0;i<events.endCount;++i){const auto pair=identity(events.endEvents[i].shapeIdA,events.endEvents[i].shapeIdB);contact_candidates.erase(pair);}
        // One fixture ending must not hide another fixture still touching on
        // the same compound body. Reconcile these pairs from current manifolds.
        for(auto it=contact_candidates.begin();it!=contact_candidates.end();) {
            auto a=index.find(it->first),b=index.find(it->second);
            if(a!=index.end()&&b!=index.end()&&records[a->second].source.body!="trigger"&&records[b->second].source.body!="trigger"&&
               (records[a->second].source.physics2d.contains("polygons")||records[b->second].source.physics2d.contains("polygons")))it=contact_candidates.erase(it);
            else ++it;
        }
        for(const auto& record:records)if(has_body(record)&&record.source.physics2d.contains("polygons")) {
            const int capacity=b2Body_GetContactCapacity(record.box_body);
            std::vector<b2ContactData> data(capacity);
            const int count=b2Body_GetContactData(record.box_body,data.data(),capacity);
            for(int i=0;i<count;++i){const auto pair=identity(data[i].shapeIdA,data[i].shapeIdB);if(!pair.first.empty()&&!pair.second.empty())contact_candidates.insert(pair);}
        }
        const auto sensors=b2World_GetSensorEvents(box.id);
        for(int i=0;i<sensors.beginCount;++i){const auto pair=identity(sensors.beginEvents[i].sensorShapeId,sensors.beginEvents[i].visitorShapeId);if(!pair.first.empty()&&!pair.second.empty())contact_candidates.insert(pair);}
        for(int i=0;i<sensors.endCount;++i){const auto pair=identity(sensors.endEvents[i].sensorShapeId,sensors.endEvents[i].visitorShapeId);contact_candidates.erase(pair);}
        for(auto it=contact_candidates.begin();it!=contact_candidates.end();){
            if(!index.contains(it->first)||!index.contains(it->second)||!has_body(records[index.at(it->first)])||!has_body(records[index.at(it->second)]))it=contact_candidates.erase(it);else++it;}
    }
    contacts=Json::array();
    auto emit=[&](const auto& pair,const char* phase){if(contacts.size()<256)contacts.push_back({{"a",pair.first},{"b",pair.second},{"phase",phase},{"tick",tick+1}});};
    for(const auto& pair:contact_candidates)emit(pair,contact_pairs.contains(pair)?"stay":"enter");
    for(const auto& pair:contact_pairs)if(!contact_candidates.contains(pair))emit(pair,"exit");
    contact_pairs=contact_candidates;
}
static int script_contacts(lua_State* vm){auto& self=owner(vm);const std::string id=lua_isnoneornil(vm,1)?"":luaL_checkstring(vm,1);Json result=Json::array();
    for(const auto& contact:self.contacts)if(id.empty()||contact["a"]==id||contact["b"]==id)result.push_back(contact);push_json(vm,result);return 1;}
