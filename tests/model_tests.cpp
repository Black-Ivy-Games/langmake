#include "langmake/model.hpp"
#include "langmake/asset_runtime.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using lm::Json;
int checks=0;
void require(bool value,const std::string&why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f,const std::string&why){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,why);}
lm::AssetSource source(Json j){lm::AssetSource s;s.type="model";s.document=std::move(j);return s;}
Json native(){return Json::parse(R"({
 "schema_version":1,
 "joints":[{"id":"hand","parent":"root","translation":[0,2,0]},{"id":"root"}],
 "parts":[{"id":"arm","joint":"hand","color":"#AABBCC","geometry":{"op":"box","size":[1,2,1]}}],
 "animations":{
  "rise":{"duration":1,"loop":true,"tracks":{"hand":{"translation":[{"time":0,"value":[0,2,0]},{"time":1,"value":[0,4,0]}]}},"markers":[{"name":"hit","time":0.5}]},
  "stay":{"duration":1,"loop":false,"tracks":{}}
 },"default_animation":"rise"
})");}
bool near(float a,float b){return std::abs(a-b)<.003f;}
void append_u32(std::string&out,uint32_t n){for(int i=0;i<4;++i)out.push_back(char((n>>(8*i))&255));}
struct Fixture {Json document;std::string buffer;};
Fixture gltf(){Fixture f;f.document={{"asset",{{"version","2.0"}}},{"buffers",Json::array()},{"bufferViews",Json::array()},{"accessors",Json::array()}};
    auto floats=[&](const std::vector<float>&v,const char*type,int components){while(f.buffer.size()%4)f.buffer.push_back(0);const auto offset=f.buffer.size();for(float value:v){uint32_t n;std::memcpy(&n,&value,4);append_u32(f.buffer,n);}const auto view=f.document["bufferViews"].size();f.document["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",v.size()*4}});const auto accessor=f.document["accessors"].size();f.document["accessors"].push_back({{"bufferView",view},{"componentType",5126},{"count",v.size()/components},{"type",type}});return accessor;};
    const auto position=floats({0,0,0,1,0,0,0,1,0},"VEC3",3);
    const auto weights=floats({0,1,0,0,0,1,0,0,0,1,0,0},"VEC4",4);
    auto offset=f.buffer.size();for(int i=0;i<3;++i){f.buffer.push_back(0);f.buffer.push_back(1);f.buffer.push_back(0);f.buffer.push_back(0);}const auto view=f.document["bufferViews"].size();f.document["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",12}});const auto joints=f.document["accessors"].size();f.document["accessors"].push_back({{"bufferView",view},{"componentType",5121},{"count",3},{"type","VEC4"}});
    const auto binds=floats({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,-1,0,1},"MAT4",16);
    const auto times=floats({0,1},"SCALAR",1);
    const auto translations=floats({0,1,0,0,2,0},"VEC3",3);
    f.document["buffers"].push_back({{"uri","skin.bin"},{"byteLength",f.buffer.size()}});
    f.document["nodes"]=Json::array({{{"name","root"},{"children",{1}}},{{"name","tip"},{"translation",{0,1,0}}},{{"name","mesh"},{"mesh",0},{"skin",0}}});
    f.document["skins"]=Json::array({{{"joints",{0,1}},{"inverseBindMatrices",binds}}});
    f.document["meshes"]=Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",position},{"JOINTS_0",joints},{"WEIGHTS_0",weights}}}}})}}});
    f.document["animations"]=Json::array({{{"name","move"},{"samplers",Json::array({{{"input",times},{"output",translations},{"interpolation","LINEAR"}}})},{"channels",Json::array({{{"sampler",0},{"target",{{"node",1},{"path","translation"}}}}})}}});
    return f;
}
lm::AssetSource imported(const Fixture&f){auto s=source({{"schema_version",1},{"source","models/skin.gltf"}});s.inputs["models/skin.gltf"]=f.document.dump();s.inputs["models/skin.bin"]=f.buffer;return s;}
std::string glb(Fixture f){f.document["buffers"][0].erase("uri");auto json=f.document.dump();while(json.size()%4)json+=' ';while(f.buffer.size()%4)f.buffer.push_back(0);std::string out;append_u32(out,0x46546c67);append_u32(out,2);append_u32(out,uint32_t(12+8+json.size()+8+f.buffer.size()));append_u32(out,uint32_t(json.size()));append_u32(out,0x4e4f534a);out+=json;append_u32(out,uint32_t(f.buffer.size()));append_u32(out,0x004e4942);out+=f.buffer;return out;}
void persist_fixture(const std::filesystem::path& root) {
    auto f=gltf();
    f.document["materials"]=Json::array({{{"doubleSided",true},{"pbrMetallicRoughness",{{"baseColorFactor",{.25,1.,.65,1.}},{"metallicFactor",0.}}}}});
    f.document["meshes"][0]["primitives"][0]["material"]=0;
    f.document["scenes"]=Json::array({{{"nodes",{0,2}}}});f.document["scene"]=0;
    auto write=[&](const std::string& name,const std::string& bytes){auto path=root/name;std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary|std::ios::trunc);stream.write(bytes.data(),bytes.size());if(!stream)throw std::runtime_error("Cannot write model fixture: "+path.string());};
    write("models/skin.gltf",f.document.dump(2));write("models/skin.bin",f.buffer);write("models/skin.glb",glb(f));
    write("assets/json.model.json",Json{{"schema_version",1},{"source","models/skin.gltf"},{"default_animation","move"}}.dump(2));
    write("assets/binary.model.json",Json{{"schema_version",1},{"source","models/skin.glb"},{"default_animation","move"}}.dump(2));
    write("scripts/game.luau","return {}\n");
    Json project{{"schema_version",2},{"dimension","3d"},{"title","Skinned glTF and GLB evidence"},{"script","scripts/game.luau"},{"gravity",{0,0,0}},
        {"game_camera",{{"mode","fixed"},{"position",{0.,1.3,7.}},{"target",{0.,1.3,0.}},{"smoothing",0}}},
        {"assets",{{"json_skin",{{"type","model"},{"path","assets/json.model.json"}}},{"binary_skin",{{"type","model"},{"path","assets/binary.model.json"}}}}},
        {"entities",Json::array({{{"id","gltf"},{"body","none"},{"asset","json_skin"},{"animation","move"},{"position",{-1.7,0.,0.}},{"size",{1.5,1.5,1.5}}},
                                {{"id","glb"},{"body","none"},{"asset","binary_skin"},{"animation","move"},{"position",{.25,0.,0.}},{"size",{1.5,1.5,1.5}}},
                                {{"id","reference"},{"body","none"},{"position",{0.,-.12,0.}},{"size",{4.,.15,.15}},{"color",{.7,.75,.85,1.}}}})}};
    write("project.json",project.dump(2));
}
}
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--fixture-root")persist_fixture(argv[2]);
    else if(argc!=1&&(argc!=2||std::string(argv[1]).starts_with("--")))throw std::runtime_error("Usage: model_tests [WORKSPACE] or model_tests --fixture-root DIRECTORY");
    const auto doc=native();auto model=lm::compile_model(source(doc));
    require(model.joints.size()==2&&model.primitives.size()==1,"Native parts/joints missing");
    require(near(model.minimum[1],1)&&near(model.maximum[1],3),"Rest bounds must not fit or use default clip");
    auto pose=lm::sample_model_pose(model,"rise",.5);
    require(near(lm::model_joint_transform(model,pose,"hand")[13],3),"Ozz joint order/remapping incorrect");
    auto vertices=lm::deform_model_primitive(model.primitives[0],pose);
    float maximum=-100;for(const auto&v:vertices)maximum=std::max(maximum,v.position[1]);require(near(maximum,4),"Rigid attached part did not follow joint");
    pose=lm::sample_model_pose(model,"rise",.5,"stay",.5,.5f);
    require(near(pose.joints[0][13],2.5f),"Cross-fade did not blend local transforms");
    require(near(lm::sample_model_pose(model,"rise",1.25).joints[0][13],2.5),"Loop wrap incorrect");
    require(near(lm::sample_model_pose(model,"stay",200).joints[0][13],2),"Nonlooping rest channels changed");
    auto events=lm::model_markers(model,"rise",.49,2.51);require(events.size()==3&&events[0]["time"]==.5&&events[2]["time"]==2.5,"Markers lost across loops");
    require(lm::model_markers(model,"rise",.5,.5).empty(),"Markers fire twice on closed lower bound");
    rejects([&]{lm::model_markers(model,"rise",0,10000);},"Unbounded marker query accepted");
    rejects([&]{lm::sample_model_pose(model,"missing",0);},"Unknown clip accepted");
    rejects([&]{lm::sample_model_pose(model,"rise",std::numeric_limits<double>::infinity());},"Nonfinite pose time accepted");
    rejects([&]{lm::model_joint_transform(model,pose,"absent");},"Unknown attachment accepted");
    require(lm::inspect_model(model)["animations"]["rise"]["markers"].size()==1,"Inspection omits animation markers");
    auto invalid=doc;invalid["joints"][1]["parent"]="hand";rejects([&]{lm::compile_model(source(invalid));},"Cyclic native skeleton accepted");
    invalid=doc;invalid["joints"][0]["rotation"]={0,0,0,0};rejects([&]{lm::compile_model(source(invalid));},"Zero quaternion accepted");
    invalid=doc;invalid["animations"]["rise"]["tracks"]["hand"]["translation"][1]["time"]=0;rejects([&]{lm::compile_model(source(invalid));},"Unsorted keys accepted");
    invalid=doc;invalid["parts"][0]["joint"]="missing";rejects([&]{lm::compile_model(source(invalid));},"Unknown part joint accepted");
    invalid=doc;invalid["joints"][0]["scale"]={0,1,1};rejects([&]{lm::compile_model(source(invalid));},"Zero joint scale accepted");
    invalid=doc;invalid["partz"]=Json::array();rejects([&]{lm::compile_model(source(invalid));},"Unknown native field accepted");
    auto fixture=gltf();auto s=imported(fixture);auto paths=lm::model_dependencies(s);require(paths==std::vector<std::string>({"models/skin.bin","models/skin.gltf"}),"Imported source graph misses buffer");
    auto skin=lm::compile_model(s);require(skin.imported&&skin.joints.size()==3&&skin.primitives.size()==1,"glTF skin import failed");
    auto rest=lm::model_mesh_at(skin,"@rest",0);require(near(rest.minimum[1],0)&&near(rest.maximum[1],1),"Inverse binds do not preserve rest geometry");
    auto animated=lm::model_mesh_at(skin,"move",.5);require(near(animated.minimum[1],.5)&&near(animated.maximum[1],1.5),"Skinned animation produced wrong geometry");
    auto binary=source({{"schema_version",1},{"source","skin.glb"}});binary.inputs["skin.glb"]=glb(fixture);auto binary_model=lm::compile_model(binary);require(near(lm::model_mesh_at(binary_model,"move",.5).minimum[1],.5),"GLB import differs from glTF");
    auto broken=fixture;broken.document["buffers"][0]["uri"]="../escape.bin";rejects([&]{lm::model_dependencies(imported(broken));},"Parent traversal URI accepted");
    broken=fixture;broken.document["buffers"][0]["uri"]="https://example.com/data.bin";rejects([&]{lm::model_dependencies(imported(broken));},"Remote source URI accepted");
    broken=fixture;broken.document["buffers"][0]["uri"]="%2e%2e/escape.bin";rejects([&]{lm::model_dependencies(imported(broken));},"Encoded parent traversal accepted");
    broken=fixture;broken.document["extensionsRequired"]={"KHR_draco_mesh_compression"};rejects([&]{lm::compile_model(imported(broken));},"Unsupported required extension silently accepted");
    broken=fixture;broken.document["animations"][0]["samplers"][0]["interpolation"]="CUBICSPLINE";rejects([&]{lm::compile_model(imported(broken));},"Unsupported interpolation silently accepted");
    broken=fixture;broken.buffer.resize(20);rejects([&]{lm::compile_model(imported(broken));},"Truncated buffer accepted");
    broken=fixture;broken.document["accessors"][0]["count"]=100001;rejects([&]{lm::compile_model(imported(broken));},"Oversized vertex count accepted");
    broken=fixture;broken.document["nodes"][1]["children"]={0};rejects([&]{lm::compile_model(imported(broken));},"Cyclic imported hierarchy accepted");
    require(lm::asset_schema("model")["properties"].contains("joints"),"Model schema undiscoverable");
    lm::Project project;project.assets["hero"]=source(doc);lm::Entity entity;entity.id="hero";entity.asset="hero";entity.animation="rise";project.entities.push_back(entity);auto compiled=lm::compile_assets(project);require(compiled.models.contains("hero"),"Project asset compiler omits models");
    project.entities[0].animation="unknown";rejects([&]{lm::compile_assets(project);},"Project accepts unknown model clip");
    // A useful repeatability/performance smoke: 128 characters for 120 ticks.
    std::vector<std::unique_ptr<lm::ModelSampler>>samplers;for(int i=0;i<128;++i)samplers.push_back(std::make_unique<lm::ModelSampler>(model));auto start=std::chrono::steady_clock::now();double sum=0;for(int tick=0;tick<120;++tick)for(auto&sampler:samplers)sum+=sampler->sample("rise",tick/60.0).joints[0][13];const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();require(std::isfinite(sum)&&seconds<10,"128-character sampling smoke exceeded 10 seconds");
    std::cout<<Json{{"ok",true},{"checks",checks},{"sampling_128_characters_120_ticks_seconds",seconds}}.dump()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
