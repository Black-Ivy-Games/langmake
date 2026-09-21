#include "langmake/model.hpp"
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb/stb_image.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/memory/unique_ptr.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>

namespace lm {
struct ModelAnimationData {
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::map<std::string,ozz::unique_ptr<ozz::animation::Animation>> clips;
    std::vector<std::size_t> runtime_to_source;
};
namespace {
using RawAnimation = ozz::animation::offline::RawAnimation;
constexpr std::size_t max_vertices = 100000, max_indices = 300000, max_keys = 65536;
[[noreturn]] void fail(const std::string& path,const std::string& message) {
    throw std::runtime_error(Json{{"code","MODEL_INVALID"},{"path",path},{"message",message}}.dump());
}
void fields(const Json& obj,std::initializer_list<const char*> allowed,const std::string& path) {
    if(!obj.is_object()) fail(path,"Expected object");
    for(auto it=obj.begin();it!=obj.end();++it) if(std::none_of(allowed.begin(),allowed.end(),[&](auto key){return key==it.key();})) fail(path+"/"+it.key(),"Unknown field");
}
std::string id(const Json& j,const std::string& path) {
    if(!j.is_string()) fail(path,"Expected ID string");
    auto s=j.get<std::string>();
    if(s.empty()||s.size()>128||s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-. ")!=std::string::npos) fail(path,"ID must contain 1..128 ASCII letters, digits, spaces, _, - or .");
    return s;
}
double number(const Json& j,double low,double high,const std::string& path) {
    if(!j.is_number()) fail(path,"Expected number");
    double n=j.get<double>(); if(!std::isfinite(n)||n<low||n>high) fail(path,"Number outside supported range"); return n;
}
template<std::size_t N> std::array<float,N> array(const Json& j,float low,float high,const std::string& path) {
    if(!j.is_array()||j.size()!=N) fail(path,"Incorrect vector length");
    std::array<float,N> v{};for(std::size_t k=0;k<N;++k)v[k]=float(number(j[k],low,high,path+"/"+std::to_string(k)));return v;
}
Matrix4 identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix4 multiply(const Matrix4&a,const Matrix4&b) {
    Matrix4 m{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)m[c*4+r]+=a[k*4+r]*b[c*4+k];return m;
}
Vec3 transform(const Matrix4&m,const Vec3&p,bool direction=false) {
    Vec3 v{};for(int r=0;r<3;++r)v[r]=m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+(direction?0:m[12+r]);return v;
}
Vec3 normal_transform(const Matrix4&m,const Vec3&n) {
    // Cofactor matrix = inverse transpose times determinant; divide to preserve
    // reflected transforms as well as nonuniform scale.
    const float a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(std::abs(det)<1e-12f) return {0,1,0};
    return {((e*i-f*h)*n[0]+(f*g-d*i)*n[1]+(d*h-e*g)*n[2])/det,
        ((c*h-b*i)*n[0]+(a*i-c*g)*n[1]+(b*g-a*h)*n[2])/det,
        ((b*f-c*e)*n[0]+(c*d-a*f)*n[1]+(a*e-b*d)*n[2])/det};
}
void normalize(Vec3& v) {float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if(n>1e-10f)for(auto&x:v)x/=n;else v={0,1,0};}
void quaternion(std::array<float,4>&q,const std::string&path) {
    double n=0;for(float x:q)n+=double(x)*x;if(!std::isfinite(n)||n<1e-12)fail(path,"Quaternion cannot have zero length");for(auto&x:q)x=float(x/std::sqrt(n));
}
std::string source_path(const Json& doc) {
    if(!doc.contains("source"))return {};
    if(!doc["source"].is_string())fail("/source","Expected project-relative glTF/GLB path");
    const auto s=doc["source"].get<std::string>();
    if(s.empty()||s.size()>1024||s.find_first_of(":\\")!=std::string::npos||s.find('\0')!=std::string::npos||s.front()=='/')fail("/source","Source path must stay inside the project");
    std::filesystem::path p(s);for(const auto& part:p)if(part==".."||part==".langmake")fail("/source","Source traversal/reserved paths are forbidden");
    return p.lexically_normal().generic_string();
}
std::string dependency_path(const std::string& base,const std::string& uri) {
    std::string decoded=uri; const auto length=cgltf_decode_uri(decoded.data()); decoded.resize(length);
    if(decoded.empty()||decoded.find_first_of(":\\")!=std::string::npos||decoded.front()=='/'||decoded.find('\0')!=std::string::npos)fail("/source","External resources must be project-relative files");
    std::filesystem::path path(decoded);for(const auto& p:path)if(p==".."||p==".langmake")fail("/source","External resource traversal/reserved paths are forbidden");
    return (std::filesystem::path(base).parent_path()/path).lexically_normal().generic_string();
}
using Gltf = std::unique_ptr<cgltf_data,decltype(&cgltf_free)>;
Gltf parse_gltf(const std::string& bytes) {
    if(bytes.size()>64*1024*1024)fail("/source","glTF/GLB source exceeds 64 MiB");
    cgltf_options options{};cgltf_data* data=nullptr;
    if(cgltf_parse(&options,bytes.data(),bytes.size(),&data)!=cgltf_result_success)fail("/source","Cannot parse glTF 2.0 document");
    return Gltf(data,cgltf_free);
}
void validate_buffers(const cgltf_data&data) {
    if(data.buffers_count>256||data.buffer_views_count>4096||data.accessors_count>4096)fail("/source","Too many buffers, views or accessors");
    // Check subtraction/division bounds before cgltf_validate performs products.
    for(std::size_t i=0;i<data.buffer_views_count;++i){const auto&v=data.buffer_views[i];
        if(!v.buffer||v.has_meshopt_compression||v.offset>v.buffer->size||v.size>v.buffer->size-v.offset)fail("/source/bufferViews","Invalid or compressed buffer view");
    }
    for(std::size_t i=0;i<data.accessors_count;++i){const auto&a=data.accessors[i];
        const auto element=cgltf_calc_size(a.type,a.component_type);
        if(a.is_sparse||!a.buffer_view||!element||a.count==0||a.count>max_indices||a.stride<element||a.stride>256)fail("/source/accessors","Unsupported sparse, empty, oversized or invalid accessor");
        const auto size=a.buffer_view->size;
        if(a.offset>size||element>size-a.offset||(a.count-1)>(size-a.offset-element)/a.stride)fail("/source/accessors","Accessor exceeds its buffer view");
    }
}
std::vector<unsigned char> base64(const std::string& uri) {
    const auto comma=uri.find(',');
    if(comma==std::string::npos||uri.substr(0,comma).find(";base64")==std::string::npos)fail("/source","Only base64 data URIs are supported");
    const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> out;unsigned bits=0,value=0;bool ended=false;
    for(std::size_t i=comma+1;i<uri.size();++i){char ch=uri[i];if(ch=='='){ended=true;continue;}auto n=alphabet.find(ch);if(ended||n==std::string::npos)fail("/source","Invalid base64 data URI");value=(value<<6)|unsigned(n);bits+=6;if(bits>=8){bits-=8;out.push_back(static_cast<unsigned char>((value>>bits)&255));}}
    if(out.size()>64*1024*1024)fail("/source","Decoded buffer exceeds 64 MiB");return out;
}
std::vector<unsigned char> resource(const AssetSource& source,const std::string& base,const char* uri) {
    if(!uri)fail("/source","Missing resource URI");
    if(std::string(uri).starts_with("data:"))return base64(uri);
    const auto key=dependency_path(base,uri);auto found=source.inputs.find(key);
    if(found==source.inputs.end())fail("/source","Missing registered resource: "+key);
    if(found->second.size()>64*1024*1024)fail("/source","Resource exceeds 64 MiB: "+key);
    return {found->second.begin(),found->second.end()};
}
std::size_t joint_index(const ModelAsset&m,const std::string&name) {
    for(std::size_t i=0;i<m.joints.size();++i)if(m.joints[i].id==name)return i;
    fail("/joints","Unknown joint: "+name);
}
ozz::math::Transform rest(const ModelJoint& j) {
    return {{j.translation[0],j.translation[1],j.translation[2]},{j.rotation[0],j.rotation[1],j.rotation[2],j.rotation[3]},{j.scale[0],j.scale[1],j.scale[2]}};
}
void fill_rest(RawAnimation::JointTrack&track,const ModelJoint&j) {
    auto t=rest(j);if(track.translations.empty())track.translations.push_back({0,t.translation});if(track.rotations.empty())track.rotations.push_back({0,t.rotation});if(track.scales.empty())track.scales.push_back({0,t.scale});
}
std::shared_ptr<ModelAnimationData> build_skeleton(const ModelAsset&m) {
    if(m.joints.empty()||m.joints.size()>128)fail("/joints","Expected 1..128 joints/nodes");
    ozz::animation::offline::RawSkeleton raw;std::vector<int>state(m.joints.size());
    std::function<void(std::size_t,ozz::animation::offline::RawSkeleton::Joint&)> add=[&](std::size_t i,auto&out){
        if(state[i])fail("/joints","Cyclic joint hierarchy");state[i]=1;const auto&j=m.joints[i];out.name=j.id.c_str();out.transform=rest(j);
        for(std::size_t k=0;k<m.joints.size();++k)if(m.joints[k].parent==int(i)){out.children.emplace_back();add(k,out.children.back());}
    };
    for(std::size_t i=0;i<m.joints.size();++i)if(m.joints[i].parent==-1){raw.roots.emplace_back();add(i,raw.roots.back());}
    if(std::find(state.begin(),state.end(),0)!=state.end())fail("/joints","Cyclic or invalid parent hierarchy");
    auto runtime=std::make_shared<ModelAnimationData>();runtime->skeleton=ozz::animation::offline::SkeletonBuilder{}(raw);
    if(!runtime->skeleton)fail("/joints","Cannot compile skeleton");
    for(auto name:runtime->skeleton->joint_names())runtime->runtime_to_source.push_back(joint_index(m,name));return runtime;
}
void build_clip(ModelAsset&m,ModelAnimationData&runtime,const std::string&name,RawAnimation raw,ModelClip clip) {
    if(m.animations.size()>=64)fail("/animations","At most 64 clips are supported");
    RawAnimation sorted;sorted.name=name.c_str();sorted.duration=float(clip.duration);sorted.tracks.resize(m.joints.size());
    for(std::size_t k=0;k<m.joints.size();++k){auto source=runtime.runtime_to_source[k];fill_rest(raw.tracks[source],m.joints[source]);sorted.tracks[k]=std::move(raw.tracks[source]);}
    auto compiled=ozz::animation::offline::AnimationBuilder{}(sorted);if(!compiled)fail("/animations/"+name,"Animation keys must be strictly increasing and inside clip duration");
    if(m.animations.contains(name))fail("/animations","Duplicate animation name: "+name);
    runtime.clips.emplace(name,std::move(compiled));m.animations.emplace(name,std::move(clip));
}
void native_model(ModelAsset&m,const Json&doc) {
    if(!doc.contains("joints")||!doc["joints"].is_array()||doc["joints"].empty()||doc["joints"].size()>128)fail("/joints","Expected 1..128 joints");
    std::vector<std::string> parents;std::set<std::string>names;
    for(const auto&v:doc["joints"]){fields(v,{"id","parent","translation","rotation","scale"},"/joints");ModelJoint j;j.id=id(v.at("id"),"/joints/id");if(!names.insert(j.id).second)fail("/joints","Duplicate joint ID");parents.push_back(v.contains("parent")?id(v["parent"],"/joints/parent"):"");if(v.contains("translation"))j.translation=array<3>(v["translation"],-10000,10000,"/joints/translation");if(v.contains("rotation")){j.rotation=array<4>(v["rotation"],-1,1,"/joints/rotation");quaternion(j.rotation,"/joints/rotation");}if(v.contains("scale"))j.scale=array<3>(v["scale"],.001f,1000,"/joints/scale");m.joints.push_back(j);}
    for(std::size_t i=0;i<m.joints.size();++i)if(!parents[i].empty())m.joints[i].parent=int(joint_index(m,parents[i]));
    auto runtime=build_skeleton(m);
    if(!doc.contains("parts")||!doc["parts"].is_array()||doc["parts"].empty()||doc["parts"].size()>64)fail("/parts","Expected 1..64 parts");
    names.clear();std::size_t count=0;
    for(const auto&part:doc["parts"]){fields(part,{"id","joint","color","geometry"},"/parts");const auto name=id(part.at("id"),"/parts/id");if(!names.insert(name).second)fail("/parts","Duplicate part ID");const auto joint=joint_index(m,id(part.at("joint"),"/parts/joint"));Json mesh_part=part;mesh_part.erase("joint");AssetSource mesh_source;mesh_source.type="mesh";mesh_source.document={{"schema_version",1},{"parts",Json::array({mesh_part})}};auto mesh=compile_mesh(mesh_source);count+=mesh.vertices.size();if(count>max_vertices)fail("/parts","Model exceeds 100000 vertices");ModelPrimitive primitive;primitive.id=name;primitive.material=m.materials.size();m.materials.emplace_back();primitive.indices=std::move(mesh.indices);for(const auto&v:mesh.vertices){ModelVertex mv;mv.position=v.position;mv.normal=v.normal;mv.color=v.color;mv.joints[0]=uint16_t(joint);primitive.vertices.push_back(mv);}m.primitives.push_back(std::move(primitive));}
    std::size_t total_keys=0;
    if(doc.contains("animations")){
        if(!doc["animations"].is_object()||doc["animations"].size()>64)fail("/animations","Expected up to 64 named animations");
        for(auto it=doc["animations"].begin();it!=doc["animations"].end();++it){const auto name=id(it.key(),"/animations");const auto&v=it.value();fields(v,{"duration","loop","tracks","markers"},"/animations/"+name);ModelClip clip;clip.duration=number(v.at("duration"),.001,600,"/animations/duration");if(v.contains("loop")){if(!v["loop"].is_boolean())fail("/animations/loop","Expected boolean");clip.loop=v["loop"].get<bool>();}RawAnimation raw;raw.tracks.resize(m.joints.size());
            if(v.contains("tracks")){if(!v["tracks"].is_object()||v["tracks"].size()>128)fail("/animations/tracks","Expected joint track map");for(auto t=v["tracks"].begin();t!=v["tracks"].end();++t){auto j=joint_index(m,t.key());fields(t.value(),{"translation","rotation","scale"},"/animations/tracks/"+t.key());for(auto channel=t.value().begin();channel!=t.value().end();++channel){if(!channel->is_array()||channel->empty()||channel->size()>4096)fail("/animations/tracks","Expected 1..4096 keys");total_keys+=channel->size();if(total_keys>max_keys)fail("/animations","Total key budget is 65536");for(const auto&key:*channel){fields(key,{"time","value"},"/animations/tracks/key");float time=float(number(key.at("time"),0,clip.duration,"/animations/tracks/time"));if(channel.key()=="rotation"){auto q=array<4>(key.at("value"),-1,1,"/animations/rotation");quaternion(q,"/animations/rotation");raw.tracks[j].rotations.push_back({time,{q[0],q[1],q[2],q[3]}});}else{auto value=array<3>(key.at("value"),channel.key()=="scale"?.001f:-10000.f,channel.key()=="scale"?1000.f:10000.f,"/animations/value");if(channel.key()=="translation")raw.tracks[j].translations.push_back({time,{value[0],value[1],value[2]}});else raw.tracks[j].scales.push_back({time,{value[0],value[1],value[2]}});}}}}}
            if(v.contains("markers")){if(!v["markers"].is_array()||v["markers"].size()>128)fail("/animations/markers","Expected at most 128 markers");for(const auto&marker:v["markers"]){fields(marker,{"name","time"},"/animations/markers");clip.markers.push_back({id(marker.at("name"),"/animations/markers/name"),number(marker.at("time"),0,clip.duration,"/animations/markers/time")});}std::stable_sort(clip.markers.begin(),clip.markers.end(),[](auto&a,auto&b){return a.time<b.time;});}
            build_clip(m,*runtime,name,std::move(raw),std::move(clip));
        }
    }
    m.animation_keys=total_keys;m.runtime=runtime;
}
const cgltf_accessor* attribute(const cgltf_primitive&p,cgltf_attribute_type type,int index=0) {for(std::size_t i=0;i<p.attributes_count;++i)if(p.attributes[i].type==type&&p.attributes[i].index==index)return p.attributes[i].data;return nullptr;}
void read_float(const cgltf_accessor*a,std::size_t i,float*out,std::size_t count) {if(!a||a->is_sparse||i>=a->count||!cgltf_accessor_read_float(a,i,out,count))fail("/source/accessors","Invalid or unsupported sparse accessor");for(std::size_t k=0;k<count;++k)if(!std::isfinite(out[k])||std::abs(out[k])>1e8f)fail("/source/accessors","Nonfinite or oversized accessor value");}
void imported_model(ModelAsset&m,const AssetSource&source,const std::string&path) {
    auto found=source.inputs.find(path);if(found==source.inputs.end())fail("/source","Missing registered model source: "+path);auto data=parse_gltf(found->second);m.imported=true;
    for(std::size_t i=0;i<data->extensions_required_count;++i)if(std::string(data->extensions_required[i])!="KHR_materials_unlit")fail("/source/extensionsRequired","Unsupported required extension: "+std::string(data->extensions_required[i]));
    if(data->nodes_count==0||data->nodes_count>128||data->animations_count>64||data->materials_count>128||data->images_count>64||data->buffers_count>256||data->buffer_views_count>4096||data->accessors_count>4096)fail("/source","Model exceeds node/clip/material/image/buffer/accessor budget");
    std::vector<std::vector<unsigned char>>buffers(data->buffers_count);std::size_t bytes=0;
    for(std::size_t i=0;i<data->buffers_count;++i){auto&b=data->buffers[i];if(b.size>64*1024*1024)fail("/source/buffers","Buffer exceeds 64 MiB");if(b.uri)buffers[i]=resource(source,path,b.uri);else if(i==0&&data->bin&&data->bin_size>=b.size)buffers[i].assign(static_cast<const unsigned char*>(data->bin),static_cast<const unsigned char*>(data->bin)+b.size);else fail("/source/buffers","Missing GLB binary buffer");if(buffers[i].size()<b.size)fail("/source/buffers","Buffer is truncated");bytes+=buffers[i].size();if(bytes>128*1024*1024)fail("/source/buffers","Decoded buffer budget is 128 MiB");b.data=buffers[i].data();b.data_free_method=cgltf_data_free_method_none;}
    validate_buffers(*data);
    if(cgltf_validate(data.get())!=cgltf_result_success)fail("/source","glTF accessor/index/hierarchy validation failed");
    std::set<std::string>names;
    for(std::size_t i=0;i<data->nodes_count;++i){const auto&n=data->nodes[i];ModelJoint j;j.id=n.name&&*n.name?n.name:"node-"+std::to_string(i);if(j.id.size()>128)fail("/source/nodes","Node name exceeds 128 bytes");if(!names.insert(j.id).second){j.id+="-"+std::to_string(i);if(!names.insert(j.id).second)fail("/source/nodes","Ambiguous node name");}j.parent=n.parent?int(n.parent-data->nodes):-1;if(n.has_matrix){ozz::math::Float4x4 matrix;for(int c=0;c<4;++c)matrix.cols[c]=ozz::math::simd_float4::LoadPtrU(n.matrix+c*4);ozz::math::Transform t;if(!ozz::math::ToAffine(matrix,&t.translation,&t.rotation,&t.scale))fail("/source/nodes","Node matrix cannot be represented by TRS");j.translation={t.translation.x,t.translation.y,t.translation.z};j.rotation={t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w};j.scale={t.scale.x,t.scale.y,t.scale.z};}else{std::copy_n(n.translation,3,j.translation.begin());std::copy_n(n.rotation,4,j.rotation.begin());std::copy_n(n.scale,3,j.scale.begin());}quaternion(j.rotation,"/source/nodes/rotation");for(auto v:j.translation)if(!std::isfinite(v)||std::abs(v)>10000)fail("/source/nodes","Invalid translation");for(auto v:j.scale)if(!std::isfinite(v)||std::abs(v)<.001||std::abs(v)>1000)fail("/source/nodes","Invalid scale");m.joints.push_back(j);}
    auto runtime=build_skeleton(m);
    bytes=0;
    for(std::size_t i=0;i<data->images_count;++i){const auto&image=data->images[i];std::vector<unsigned char>encoded;if(image.uri)encoded=resource(source,path,image.uri);else if(image.buffer_view){const auto*v=image.buffer_view;const auto*start=static_cast<const unsigned char*>(v->buffer->data)+v->offset;encoded.assign(start,start+v->size);}else fail("/source/images","Image has no source");int w=0,h=0,components=0;if(encoded.size()>64*1024*1024||!stbi_info_from_memory(encoded.data(),int(encoded.size()),&w,&h,&components)||w<1||h<1||w>4096||h>4096)fail("/source/images","Expected PNG/JPEG image of at most 4096x4096");bytes+=std::size_t(w)*h*4;if(bytes>64*1024*1024)fail("/source/images","Decoded image budget is 64 MiB");auto*p=stbi_load_from_memory(encoded.data(),int(encoded.size()),&w,&h,&components,4);if(!p)fail("/source/images","Image decoding failed");SpriteFrame decoded;decoded.id="image-"+std::to_string(i);decoded.width=w;decoded.height=h;decoded.rgba.assign(p,p+std::size_t(w)*h*4);stbi_image_free(p);m.images.push_back(std::move(decoded));}
    for(std::size_t i=0;i<data->materials_count;++i){const auto&v=data->materials[i];ModelMaterial mat;mat.unlit=v.unlit;mat.double_sided=v.double_sided;mat.alpha_mode=v.alpha_mode==cgltf_alpha_mode_blend?"BLEND":v.alpha_mode==cgltf_alpha_mode_mask?"MASK":"OPAQUE";mat.alpha_cutoff=v.alpha_cutoff;if(v.has_pbr_metallic_roughness){std::copy_n(v.pbr_metallic_roughness.base_color_factor,4,mat.color.begin());const auto&tex=v.pbr_metallic_roughness.base_color_texture;if(tex.texture){if(tex.texcoord!=0||tex.has_transform)fail("/source/materials","Base color textures currently require UV0 without a transform");if(!tex.texture->image)fail("/source/materials","Texture has no decoded image");mat.image=int(tex.texture->image-data->images);}}for(float c:mat.color)if(!std::isfinite(c)||c<0||c>1)fail("/source/materials","Invalid material color");m.materials.push_back(mat);}const std::size_t default_material=m.materials.size();m.materials.emplace_back();
    std::size_t vertex_count=0,index_count=0;
    for(std::size_t n=0;n<data->nodes_count;++n){const auto&node=data->nodes[n];if(!node.mesh)continue;for(std::size_t pi=0;pi<node.mesh->primitives_count;++pi){const auto&p=node.mesh->primitives[pi];if(p.type!=cgltf_primitive_type_triangles||p.targets_count||p.has_draco_mesh_compression)fail("/source/meshes","Only uncompressed triangle primitives without morph targets are supported");if(m.primitives.size()>=128)fail("/source/meshes","At most 128 primitives");const auto*position=attribute(p,cgltf_attribute_type_position);if(!position||position->type!=cgltf_type_vec3)fail("/source/meshes","POSITION VEC3 required");vertex_count+=position->count;if(vertex_count>max_vertices)fail("/source/meshes","Model exceeds 100000 vertices");ModelPrimitive primitive;primitive.id=m.joints[n].id+"/"+std::to_string(pi);primitive.material=p.material?std::size_t(p.material-data->materials):default_material;const auto*normal=attribute(p,cgltf_attribute_type_normal);const auto*uv=attribute(p,cgltf_attribute_type_texcoord);const auto*color=attribute(p,cgltf_attribute_type_color);const auto*joints=attribute(p,cgltf_attribute_type_joints);const auto*weights=attribute(p,cgltf_attribute_type_weights);for(auto* accessor:{normal,uv,color,joints,weights})if(accessor&&accessor->count!=position->count)fail("/source/meshes","Attribute vertex counts differ");if(normal&&normal->type!=cgltf_type_vec3)fail("/source/meshes","NORMAL must be VEC3");if(uv&&uv->type!=cgltf_type_vec2)fail("/source/meshes","TEXCOORD_0 must be VEC2");if(attribute(p,cgltf_attribute_type_joints,1)||attribute(p,cgltf_attribute_type_weights,1))fail("/source/meshes","At most four skin weights per vertex");
        if(node.skin){if(!joints||!weights||joints->type!=cgltf_type_vec4||weights->type!=cgltf_type_vec4)fail("/source/skins","Skinned meshes require JOINTS_0 and WEIGHTS_0 VEC4");primitive.inverse_bind.assign(m.joints.size(),identity());if(node.skin->joints_count>128)fail("/source/skins","Too many skin joints");for(std::size_t j=0;j<node.skin->joints_count;++j){auto index=std::size_t(node.skin->joints[j]-data->nodes);if(node.skin->inverse_bind_matrices)read_float(node.skin->inverse_bind_matrices,j,primitive.inverse_bind[index].data(),16);}}
        primitive.vertices.resize(position->count);for(std::size_t v=0;v<position->count;++v){auto&out=primitive.vertices[v];read_float(position,v,out.position.data(),3);if(normal)read_float(normal,v,out.normal.data(),3);if(uv)read_float(uv,v,out.uv.data(),2);if(color){if(color->type!=cgltf_type_vec3&&color->type!=cgltf_type_vec4)fail("/source/meshes","COLOR_0 must be VEC3 or VEC4");read_float(color,v,out.color.data(),color->type==cgltf_type_vec4?4:3);}out.joints[0]=uint16_t(n);if(node.skin){cgltf_uint ji[4]{};if(joints->is_sparse||!cgltf_accessor_read_uint(joints,v,ji,4))fail("/source/skins","Invalid joint accessor");read_float(weights,v,out.weights.data(),4);double total=0;for(int k=0;k<4;++k){if(ji[k]>=node.skin->joints_count||out.weights[k]<0)fail("/source/skins","Invalid joint or negative skin weight");out.joints[k]=uint16_t(node.skin->joints[ji[k]]-data->nodes);total+=out.weights[k];}if(total<1e-9)fail("/source/skins","Skin weights cannot sum to zero");for(float&weight:out.weights)weight=float(weight/total);}}
        const std::size_t count=p.indices?p.indices->count:position->count;if(count%3)fail("/source/meshes","Triangle index count must be divisible by three");index_count+=count;if(index_count>max_indices)fail("/source/meshes","Model exceeds 300000 indices");for(std::size_t k=0;k<count;++k){auto index=p.indices?cgltf_accessor_read_index(p.indices,k):k;if(index>=position->count)fail("/source/meshes","Index outside vertex buffer");primitive.indices.push_back(uint32_t(index));}if(!normal){for(auto&v:primitive.vertices)v.normal={0,0,0};for(std::size_t k=0;k<count;k+=3){auto&a=primitive.vertices[primitive.indices[k]],&b=primitive.vertices[primitive.indices[k+1]],&c=primitive.vertices[primitive.indices[k+2]];Vec3 u{b.position[0]-a.position[0],b.position[1]-a.position[1],b.position[2]-a.position[2]},v{c.position[0]-a.position[0],c.position[1]-a.position[1],c.position[2]-a.position[2]},cross{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};for(int axis=0;axis<3;++axis){a.normal[axis]+=cross[axis];b.normal[axis]+=cross[axis];c.normal[axis]+=cross[axis];}}}for(auto&v:primitive.vertices)normalize(v.normal);m.primitives.push_back(std::move(primitive));}}
    if(m.primitives.empty())fail("/source/meshes","Model contains no mesh triangles");std::size_t total_keys=0;
    for(std::size_t a=0;a<data->animations_count;++a){const auto&animation=data->animations[a];std::string name=animation.name&&*animation.name?animation.name:"animation-"+std::to_string(a);if(name.size()>128)fail("/source/animations","Animation name exceeds 128 bytes");RawAnimation raw;raw.tracks.resize(m.joints.size());ModelClip clip;clip.duration=.001;std::set<std::pair<std::size_t,int>>used;
        for(std::size_t c=0;c<animation.channels_count;++c){const auto&channel=animation.channels[c];const auto*s=channel.sampler;if(!channel.target_node||!s||!s->input||!s->output||s->interpolation!=cgltf_interpolation_type_linear)fail("/source/animations","Only LINEAR TRS animation channels are supported");if(channel.target_path==cgltf_animation_path_type_weights||channel.target_path==cgltf_animation_path_type_invalid)fail("/source/animations","Only translation, rotation and scale animation channels are supported");auto j=std::size_t(channel.target_node-data->nodes);if(!used.insert({j,int(channel.target_path)}).second)fail("/source/animations","Duplicate target channel");if(s->input->count!=s->output->count||!s->input->count)fail("/source/animations","Key/value counts differ or are empty");total_keys+=s->input->count;if(total_keys>max_keys)fail("/source/animations","Total key budget is 65536");
            for(std::size_t k=0;k<s->input->count;++k){float time=0;read_float(s->input,k,&time,1);if(time<0||time>600)fail("/source/animations","Animation times must be in 0..600 seconds");clip.duration=std::max(clip.duration,double(time));float v[4]{};read_float(s->output,k,v,channel.target_path==cgltf_animation_path_type_rotation?4:3);if(channel.target_path==cgltf_animation_path_type_rotation){std::array<float,4>q{v[0],v[1],v[2],v[3]};quaternion(q,"/source/animations");raw.tracks[j].rotations.push_back({time,{q[0],q[1],q[2],q[3]}});}else if(channel.target_path==cgltf_animation_path_type_translation)raw.tracks[j].translations.push_back({time,{v[0],v[1],v[2]}});else{for(int axis=0;axis<3;++axis)if(std::abs(v[axis])<.001||std::abs(v[axis])>1000)fail("/source/animations","Invalid animated scale");raw.tracks[j].scales.push_back({time,{v[0],v[1],v[2]}});}}
        }build_clip(m,*runtime,name,std::move(raw),std::move(clip));
    }m.animation_keys=total_keys;m.runtime=runtime;
}
}

std::vector<std::string> model_dependencies(const AssetSource&source) {
    const auto path=source_path(source.document);if(path.empty())return {};std::vector<std::string> result{path};auto found=source.inputs.find(path);if(found==source.inputs.end())return result;auto data=parse_gltf(found->second);
    auto add=[&](const char*uri){if(uri&&!std::string(uri).starts_with("data:"))result.push_back(dependency_path(path,uri));};
    for(std::size_t i=0;i<data->buffers_count;++i)add(data->buffers[i].uri);for(std::size_t i=0;i<data->images_count;++i)add(data->images[i].uri);std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
ModelAsset compile_model(const AssetSource&source) {
    if(source.type!="model")fail("/type","Asset must have type model");
    try{const auto&doc=source.document;fields(doc,{"schema_version","type","description","source","joints","parts","animations","default_animation"},"");if(!doc.contains("schema_version")||doc["schema_version"]!=1)fail("/schema_version","Expected schema_version 1");if(doc.contains("type")&&doc["type"]!="model")fail("/type","Expected model");ModelAsset m;auto path=source_path(doc);if(path.empty())native_model(m,doc);else{if(doc.contains("joints")||doc.contains("parts")||doc.contains("animations"))fail("/source","Imported and native model definitions cannot be combined");imported_model(m,source,path);}if(doc.contains("default_animation")){m.default_animation=id(doc["default_animation"],"/default_animation");if(!m.animations.contains(m.default_animation))fail("/default_animation","Unknown animation");}else if(!m.animations.empty())m.default_animation=m.animations.begin()->first;
        // Bounds always describe rest pose, independent of the default clip.
        auto pose=sample_model_pose(m,"@rest",0);bool first=true;for(const auto&p:m.primitives)for(const auto&v:deform_model_primitive(p,pose)){if(first){m.minimum=v.position;m.maximum=v.position;first=false;}for(int k=0;k<3;++k){if(!std::isfinite(v.position[k])||std::abs(v.position[k])>100000)fail("/parts","Rest geometry exceeds finite coordinate budget");m.minimum[k]=std::min(m.minimum[k],v.position[k]);m.maximum[k]=std::max(m.maximum[k],v.position[k]);}}return m;
    }catch(const Json::exception&e){fail("",std::string("Malformed model field: ")+e.what());}
}
struct ModelSampler::Impl {
    const ModelAsset&model;std::shared_ptr<const ModelAnimationData>runtime;
    ozz::animation::SamplingJob::Context current,previous;
    std::vector<ozz::math::SoaTransform> locals,old,blended;
    std::vector<ozz::math::Float4x4>matrices;
    explicit Impl(const ModelAsset&m):model(m),runtime(m.runtime),current(int(m.joints.size())),previous(int(m.joints.size())),locals((m.joints.size()+3)/4),old(locals.size()),blended(locals.size()),matrices(m.joints.size()){if(!runtime)fail("/animation","Uncompiled model");}
    void sample(const std::string&requested,double seconds,ozz::animation::SamplingJob::Context&context,std::vector<ozz::math::SoaTransform>&out){if(!std::isfinite(seconds)||seconds<0)fail("/animation/time","Animation time must be finite and nonnegative");const auto name=requested.empty()?model.default_animation:requested;if(name=="@rest"||name.empty()){auto rest=runtime->skeleton->joint_rest_poses();std::copy(rest.begin(),rest.end(),out.begin());return;}auto found=model.animations.find(name);if(found==model.animations.end())fail("/animation","Unknown clip: "+name);const auto&clip=found->second;double at=clip.loop?std::fmod(seconds,clip.duration):std::min(seconds,clip.duration);ozz::animation::SamplingJob job;job.animation=runtime->clips.at(name).get();job.context=&context;job.ratio=float(at/clip.duration);job.output=ozz::make_span(out);if(!job.Run())fail("/animation","Pose sampling failed");}
};
ModelSampler::ModelSampler(const ModelAsset&m):impl_(std::make_unique<Impl>(m)){}
ModelSampler::~ModelSampler()=default;
ModelPose ModelSampler::sample(const std::string&clip,double seconds,const std::string&previous,double previous_seconds,float blend){auto&s=*impl_;if(!std::isfinite(blend)||blend<0||blend>1)fail("/animation/blend","Blend must be in 0..1");s.sample(clip,seconds,s.current,s.locals);auto*locals=&s.locals;if(blend<1){s.sample(previous,previous_seconds,s.previous,s.old);ozz::animation::BlendingJob::Layer layers[2];layers[0].transform=ozz::make_span(s.old);layers[0].weight=1-blend;layers[1].transform=ozz::make_span(s.locals);layers[1].weight=blend;ozz::animation::BlendingJob job;job.layers=ozz::make_span(layers);job.rest_pose=s.runtime->skeleton->joint_rest_poses();job.output=ozz::make_span(s.blended);if(!job.Run())fail("/animation","Pose blending failed");locals=&s.blended;}ozz::animation::LocalToModelJob job;job.skeleton=s.runtime->skeleton.get();job.input=ozz::make_span(*locals);job.output=ozz::make_span(s.matrices);if(!job.Run())fail("/animation","Joint hierarchy evaluation failed");ModelPose pose;pose.joints.resize(s.model.joints.size());for(std::size_t j=0;j<s.matrices.size();++j)for(int c=0;c<4;++c)ozz::math::StorePtrU(s.matrices[j].cols[c],pose.joints[s.runtime->runtime_to_source[j]].data()+4*c);return pose;}
ModelPose sample_model_pose(const ModelAsset&m,const std::string&clip,double seconds,const std::string&previous,double previous_seconds,float blend){return ModelSampler(m).sample(clip,seconds,previous,previous_seconds,blend);}
Matrix4 model_joint_transform(const ModelAsset&m,const ModelPose&pose,const std::string&joint){auto index=joint_index(m,joint);if(pose.joints.size()!=m.joints.size())fail("/animation","Pose joint count differs from model");return pose.joints[index];}
Json model_markers(const ModelAsset&m,const std::string&requested,double from,double to){if(!std::isfinite(from)||!std::isfinite(to)||from<0||to<from||to>1e12)fail("/animation/markers","Expected finite increasing nonnegative times");const auto name=requested.empty()?m.default_animation:requested;Json result=Json::array();if(name.empty())return result;auto found=m.animations.find(name);if(found==m.animations.end())fail("/animation","Unknown clip: "+name);const auto&clip=found->second;if(clip.markers.empty())return result;const double first=clip.loop?std::floor(from/clip.duration):0,last=clip.loop?std::floor(to/clip.duration):0;if(last-first>1024)fail("/animation/markers","Marker query crosses more than 1024 loops");for(double cycle=first;cycle<=last;++cycle)for(const auto&marker:clip.markers){const double at=cycle*clip.duration+marker.time;if(at>from&&at<=to){if(result.size()==128)fail("/animation/markers","Marker query exceeds 128 events");result.push_back({{"name",marker.name},{"time",at},{"clip",name}});}}return result;}
std::vector<ModelVertex> deform_model_primitive(const ModelPrimitive&p,const ModelPose&pose){std::vector<Matrix4>matrices=pose.joints;if(!p.inverse_bind.empty()){if(p.inverse_bind.size()!=matrices.size())fail("/skin","Invalid inverse bind count");for(std::size_t i=0;i<matrices.size();++i)matrices[i]=multiply(matrices[i],p.inverse_bind[i]);}auto result=p.vertices;for(auto&v:result){Vec3 point{},normal{};for(int k=0;k<4;++k)if(v.weights[k]>0){if(v.joints[k]>=matrices.size())fail("/skin","Joint index outside pose");auto a=transform(matrices[v.joints[k]],v.position),b=normal_transform(matrices[v.joints[k]],v.normal);for(int axis=0;axis<3;++axis){point[axis]+=a[axis]*v.weights[k];normal[axis]+=b[axis]*v.weights[k];}}v.position=point;normalize(normal);v.normal=normal;}return result;}
MeshAsset model_mesh_at(const ModelAsset&m,const std::string&clip,double seconds){auto pose=sample_model_pose(m,clip,seconds);MeshAsset mesh;bool first=true;for(const auto&p:m.primitives){auto vertices=deform_model_primitive(p,pose);mesh.parts.push_back(p.id);mesh.part_index_offsets.push_back(mesh.indices.size());auto base=uint32_t(mesh.vertices.size());for(auto&v:vertices){Color color=v.color;for(int k=0;k<4;++k)color[k]*=m.materials[p.material].color[k];mesh.vertices.push_back({v.position,v.normal,color});if(first){mesh.minimum=v.position;mesh.maximum=v.position;first=false;}for(int k=0;k<3;++k){mesh.minimum[k]=std::min(mesh.minimum[k],v.position[k]);mesh.maximum[k]=std::max(mesh.maximum[k],v.position[k]);}}for(auto i:p.indices)mesh.indices.push_back(base+i);}mesh.part_index_offsets.push_back(mesh.indices.size());return mesh;}
Json inspect_model(const ModelAsset&m){Json joints=Json::array(),parts=Json::array(),clips=Json::object();std::size_t vertices=0,indices=0;for(const auto&j:m.joints)joints.push_back({{"id",j.id},{"parent",j.parent<0?Json(nullptr):Json(m.joints[j.parent].id)},{"translation",j.translation},{"rotation",j.rotation},{"scale",j.scale}});for(const auto&p:m.primitives){vertices+=p.vertices.size();indices+=p.indices.size();parts.push_back({{"id",p.id},{"vertices",p.vertices.size()},{"triangles",p.indices.size()/3},{"skinned",!p.inverse_bind.empty()}});}for(const auto&[id,c]:m.animations){Json markers=Json::array();for(const auto&marker:c.markers)markers.push_back({{"name",marker.name},{"time",marker.time}});clips[id]={{"duration",c.duration},{"loop",c.loop},{"markers",markers}};}return {{"type","model"},{"imported",m.imported},{"joints",joints},{"parts",parts},{"vertices",vertices},{"triangles",indices/3},{"images",m.images.size()},{"animations",clips},{"default_animation",m.default_animation},{"bounds",{{"minimum",m.minimum},{"maximum",m.maximum}}},{"units","meters"},{"up_axis","Y"},{"skinning","cpu"}};}
Json model_schema() {
    auto vector=[](int n,double low,double high){return Json{{"type","array"},{"minItems",n},{"maxItems",n},{"items",{{"type","number"},{"minimum",low},{"maximum",high}}}};};
    Json identifier{{"type","string"},{"minLength",1},{"maxLength",128}};
    Json joint{{"type","object"},{"additionalProperties",false},{"required",{"id"}},{"properties",{
        {"id",identifier},{"parent",identifier},{"translation",vector(3,-10000,10000)},
        {"rotation",vector(4,-1,1)},{"scale",vector(3,.001,1000)}}}};
    Json tracks{{"type","object"},{"additionalProperties",false},{"properties",Json::object()}};
    for(const std::string channel:{"translation","rotation","scale"}) {
        Json key{{"type","object"},{"additionalProperties",false},{"required",{"time","value"}},
            {"properties",{{"time",{{"type","number"},{"minimum",0},{"maximum",600}}},
            {"value",channel=="rotation"?vector(4,-1,1):channel=="scale"?vector(3,.001,1000):vector(3,-10000,10000)}}}};
        tracks["properties"][channel]={{"type","array"},{"minItems",1},{"maxItems",4096},{"items",key}};
    }
    Json marker{{"type","object"},{"additionalProperties",false},{"required",{"name","time"}},
        {"properties",{{"name",identifier},{"time",{{"type","number"},{"minimum",0},{"maximum",600}}}}}};
    Json clip{{"type","object"},{"additionalProperties",false},{"required",{"duration"}},
        {"properties",{{"duration",{{"type","number"},{"minimum",.001},{"maximum",600}}},
        {"loop",{{"type","boolean"},{"default",true}}},
        {"tracks",{{"type","object"},{"maxProperties",128},{"additionalProperties",tracks}}},
        {"markers",{{"type","array"},{"maxItems",128},{"items",marker}}}}}};
    auto mesh=asset_schema("mesh");Json part=mesh["properties"]["parts"]["items"];
    part["properties"]["joint"]=identifier;part["required"].push_back("joint");
    Json props{{"schema_version",{{"const",1}}},{"type",{{"const","model"}}},
        {"description",{{"type","string"}}},{"source",{{"type","string"}}},
        {"joints",{{"type","array"},{"minItems",1},{"maxItems",128},{"items",joint}}},
        {"parts",{{"type","array"},{"minItems",1},{"maxItems",64},{"items",part}}},
        {"animations",{{"type","object"},{"maxProperties",64},{"additionalProperties",clip}}},
        {"default_animation",identifier}};
    return {{"$schema","https://json-schema.org/draft/2020-12/schema"},{"title","Langmake model recipe v1"},
        {"type","object"},{"additionalProperties",false},{"required",{"schema_version"}},
        {"properties",props},{"$defs",mesh.value("$defs",Json::object())},
        {"oneOf",Json::array({{{"required",{"source"}},{"not",{{"anyOf",Json::array({{{"required",{"joints"}}},{{"required",{"parts"}}},{{"required",{"animations"}}}})}}}},
        {{"required",{"joints","parts"}},{"not",{{"required",{"source"}}}}}})},
        {"description","Native joints use local translation, quaternion xyzw rotation and positive scale. Parts attach solid geometry to joints. Key times are strictly increasing, within clip duration, with a total 65536-key budget. Markers use (from,to] semantics. Imported glTF/GLB supports uncompressed triangles, PNG/JPEG base-color textures, four skin weights and LINEAR TRS clips. Units and pivots are retained; entity.size scales models without fitting bounds."}};
}
}
