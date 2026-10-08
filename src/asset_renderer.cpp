#include "langmake/asset_renderer.hpp"
#include "langmake/platform_shaders.hpp"
#include <bx/math.h>
#include <vs_debugdraw_fill_texture.bin.h>
#include <fs_debugdraw_fill_texture.bin.h>
#include <vs_debugdraw_fill_lit.bin.h>
#include <fs_debugdraw_fill_lit.bin.h>
#include <vs_model.bin.h>
#include <fs_model.bin.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace lm {
namespace {
struct Vertex { float x,y,z; uint8_t indices[4]; };
struct SpriteVertex { float x,y,z; uint32_t color; float u,v; };
struct SkinnedVertex { float x,y,z,nx,ny,nz; uint32_t color; float u,v; };
uint32_t color(const Color& value, float light=1) {
    auto byte=[](float v){ return uint32_t(std::clamp(v,0.f,1.f)*255); };
    return byte(value[0]*light)|(byte(value[1]*light)<<8)|(byte(value[2]*light)<<16)|(byte(value[3])<<24);
}
bgfx::ProgramHandle create_program(const uint8_t* vertex, uint32_t vertex_bytes, const uint8_t* fragment, uint32_t fragment_bytes) {
    auto vs=bgfx::createShader(bgfx::copy(vertex,vertex_bytes));
    auto fs=bgfx::createShader(bgfx::copy(fragment,fragment_bytes));
    if(!bgfx::isValid(vs)||!bgfx::isValid(fs)) {
        if(bgfx::isValid(vs)) bgfx::destroy(vs);
        if(bgfx::isValid(fs)) bgfx::destroy(fs);
        throw std::runtime_error("Cannot create native asset shaders");
    }
    auto result=bgfx::createProgram(vs,fs,true);
    if(!bgfx::isValid(result)) throw std::runtime_error("Cannot link native asset shaders");
    return result;
}
}
struct AssetRenderer::Impl {
    struct Part { uint32_t first=0, count=0; Color color{1,1,1,1}; };
    struct Mesh { bgfx::VertexBufferHandle vertices=BGFX_INVALID_HANDLE; bgfx::IndexBufferHandle indices=BGFX_INVALID_HANDLE; std::vector<Part> parts; };
    struct Model { std::vector<bgfx::IndexBufferHandle> indices; std::vector<bgfx::TextureHandle> textures; };
    struct Sampler { const ModelAsset* asset=nullptr; std::unique_ptr<ModelSampler> value; };
    CompiledAssets assets;
    struct SurfaceTexture { bgfx::TextureHandle texture=BGFX_INVALID_HANDLE; int w=0,h=0; uint64_t revision=0; };
    std::map<std::string,SurfaceTexture> surfaces;
    struct SpriteRegion { bgfx::TextureHandle texture; float u0=0,v0=0,u1=1,v1=1; };
    std::map<std::string,std::vector<SpriteRegion>> textures;
    std::vector<bgfx::TextureHandle> sprite_textures;
    std::map<std::string,Mesh> meshes;
    std::map<std::string,Model> models;
    std::map<std::string,Sampler> samplers;
    bgfx::VertexLayout mesh_layout, sprite_layout, model_layout;
    bgfx::ProgramHandle sprite_program=BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle mesh_program=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lighting=BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle model_program=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle model_material=BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white=BGFX_INVALID_HANDLE;
    ~Impl() {
        for(auto&[id,s]:surfaces)if(bgfx::isValid(s.texture))bgfx::destroy(s.texture);
        for(auto t:sprite_textures) if(bgfx::isValid(t)) bgfx::destroy(t);
        for(auto& [id,m]:meshes) {
            if(bgfx::isValid(m.vertices)) bgfx::destroy(m.vertices);
            if(bgfx::isValid(m.indices)) bgfx::destroy(m.indices);
        }
        for(auto& [id,m]:models) { for(auto h:m.indices) if(bgfx::isValid(h))bgfx::destroy(h); for(auto h:m.textures)if(bgfx::isValid(h))bgfx::destroy(h); }
        if(bgfx::isValid(sprite_program)) bgfx::destroy(sprite_program);
        if(bgfx::isValid(mesh_program)) bgfx::destroy(mesh_program);
        if(bgfx::isValid(sampler)) bgfx::destroy(sampler);
        if(bgfx::isValid(lighting)) bgfx::destroy(lighting);
        if(bgfx::isValid(model_program)) bgfx::destroy(model_program);
        if(bgfx::isValid(model_material)) bgfx::destroy(model_material);
        if(bgfx::isValid(white)) bgfx::destroy(white);
    }
    void initialize(const CompiledAssets& source) {
        assets=source;
        // All four indices are zero: the upstream lit shader selects u_model[0].
        mesh_layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::Indices,4,bgfx::AttribType::Uint8).end();
        sprite_layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
        model_layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::Normal,3,bgfx::AttribType::Float).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
        if(!assets.sprites.empty()||!assets.models.empty()) {
            sprite_program=create_program(LM_SHADER(vs_debugdraw_fill_texture),sizeof(LM_SHADER(vs_debugdraw_fill_texture)),LM_SHADER(fs_debugdraw_fill_texture),sizeof(LM_SHADER(fs_debugdraw_fill_texture)));
            sampler=bgfx::createUniform("s_texColor",bgfx::UniformType::Sampler);
            if(!bgfx::isValid(sprite_program)||!bgfx::isValid(sampler)) throw std::runtime_error("Cannot create sprite shader");
        }
        if(!assets.models.empty()) {
            model_program=create_program(vs_model_native,sizeof(vs_model_native),fs_model_native,sizeof(fs_model_native));
            model_material=bgfx::createUniform("u_modelMaterial",bgfx::UniformType::Vec4);
            const uint32_t pixel=0xffffffff;
            white=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(&pixel,4));
            if(!bgfx::isValid(model_material)||!bgfx::isValid(white))throw std::runtime_error("Cannot create model shader resources");
        }
        if(!assets.meshes.empty()) {
            mesh_program=create_program(LM_SHADER(vs_debugdraw_fill_lit),sizeof(LM_SHADER(vs_debugdraw_fill_lit)),LM_SHADER(fs_debugdraw_fill_lit),sizeof(LM_SHADER(fs_debugdraw_fill_lit)));
            lighting=bgfx::createUniform("u_params",bgfx::UniformType::Vec4,4);
            if(!bgfx::isValid(lighting)) throw std::runtime_error("Cannot create mesh lighting uniform");
        }
        size_t bytes=0, count=0;
        for(const auto& [id,sprite]:assets.sprites) {
            auto& frames=textures[id];
            auto upload=[&](int width,int height,const std::vector<uint8_t>& pixels) {
                bytes+=pixels.size();
                if(bytes>256*1024*1024) throw std::runtime_error("Preview sprite GPU budget is 256 MiB");
                auto t=bgfx::createTexture2D(uint16_t(width),uint16_t(height),false,1,bgfx::TextureFormat::RGBA8,
                    BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT,
                    bgfx::copy(pixels.data(),uint32_t(pixels.size())));
                if(!bgfx::isValid(t)) throw std::runtime_error("Cannot create sprite texture: "+id);
                sprite_textures.push_back(t);return t;
            };
            count+=sprite->frames.size();
            if(count>2048) throw std::runtime_error("Preview sprite budget is 2048 frames");
            // Atlas animation frames without changing authored pixels. Duplicated
            // gutters preserve clamp sampling at the edges of each frame.
            const auto& first=sprite->frames.front();
            const int cw=first.width+2,ch=first.height+2;
            const int limit=std::min(4096,int(bgfx::getCaps()->limits.maxTextureSize));
            const int columns=std::min(int(std::ceil(std::sqrt(double(sprite->frames.size())))),limit/cw);
            const int rows=columns ? (int(sprite->frames.size())+columns-1)/columns : limit+1;
            const bool uniform=std::all_of(sprite->frames.begin(),sprite->frames.end(),[&](const auto& f){return f.width==first.width&&f.height==first.height;});
            if(sprite->frames.size()>1&&uniform&&columns>0&&rows*ch<=limit) {
                const int width=columns*cw,height=rows*ch;
                std::vector<uint8_t> pixels(size_t(width)*height*4);
                for(size_t n=0;n<sprite->frames.size();++n) {
                    const auto& f=sprite->frames[n];const int ox=int(n%columns)*cw,oy=int(n/columns)*ch;
                    for(int y=0;y<ch;++y) for(int x=0;x<cw;++x) {
                        const auto from=(size_t(std::clamp(y-1,0,int(f.height)-1))*f.width+std::clamp(x-1,0,int(f.width)-1))*4;
                        const auto to=(size_t(oy+y)*width+ox+x)*4;
                        std::memcpy(pixels.data()+to,f.rgba.data()+from,4);
                    }
                }
                const auto texture=upload(width,height,pixels);
                for(size_t n=0;n<sprite->frames.size();++n) {
                    const int x=int(n%columns)*cw+1,y=int(n/columns)*ch+1;
                    frames.push_back({texture,float(x)/width,float(y)/height,float(x+first.width)/width,float(y+first.height)/height});
                }
                continue;
            }
            for(const auto& f:sprite->frames) {
                frames.push_back({upload(f.width,f.height,f.rgba)});
            }
        }
        for(const auto& [id,mesh]:assets.meshes) {
            std::vector<Vertex> vertices;
            vertices.reserve(mesh->vertices.size());
            for(const auto& v:mesh->vertices) {
                Vec3 p;
                for(int k=0;k<3;++k) {
                    const float extent=mesh->maximum[k]-mesh->minimum[k];
                    p[k]=(v.position[k]-(mesh->minimum[k]+mesh->maximum[k])*.5f)/(extent>0?extent:1.f);
                }
                vertices.push_back({p[0],p[1],p[2],{0,0,0,0}});
            }
            bytes+=vertices.size()*sizeof(Vertex)+mesh->indices.size()*sizeof(uint32_t);
            if(bytes>256*1024*1024) throw std::runtime_error("Preview asset GPU budget is 256 MiB");
            auto& gpu=meshes[id];
            if(mesh->part_index_offsets.size()!=mesh->parts.size()+1) throw std::runtime_error("Mesh part ranges are missing: "+id);
            for(size_t part=0;part<mesh->parts.size();++part) {
                size_t first=mesh->part_index_offsets[part], end=mesh->part_index_offsets[part+1];
                if(first>=end||end>mesh->indices.size()) throw std::runtime_error("Mesh part range is invalid: "+id);
                gpu.parts.push_back({uint32_t(first),uint32_t(end-first),mesh->vertices.at(mesh->indices.at(first)).color});
            }
            gpu.vertices=bgfx::createVertexBuffer(bgfx::copy(vertices.data(),uint32_t(vertices.size()*sizeof(Vertex))),mesh_layout);
            gpu.indices=bgfx::createIndexBuffer(bgfx::copy(mesh->indices.data(),uint32_t(mesh->indices.size()*sizeof(uint32_t))),BGFX_BUFFER_INDEX32);
            if(!bgfx::isValid(gpu.vertices)||!bgfx::isValid(gpu.indices)) throw std::runtime_error("Cannot create mesh buffers: "+id);
        }
        for(const auto& [id,asset]:assets.models) {
            auto& model=models[id];
            for(const auto& p:asset->primitives) {
                bytes+=p.indices.size()*sizeof(uint32_t);
                auto h=bgfx::createIndexBuffer(bgfx::copy(p.indices.data(),uint32_t(p.indices.size()*sizeof(uint32_t))),BGFX_BUFFER_INDEX32);
                if(!bgfx::isValid(h))throw std::runtime_error("Cannot create model index buffer: "+id);
                model.indices.push_back(h);
            }
            for(const auto& image:asset->images) {
                bytes+=image.rgba.size();
                auto h=bgfx::createTexture2D(uint16_t(image.width),uint16_t(image.height),false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(image.rgba.data(),uint32_t(image.rgba.size())));
                if(!bgfx::isValid(h))throw std::runtime_error("Cannot create model texture: "+id);
                model.textures.push_back(h);
            }
            if(bytes>256*1024*1024)throw std::runtime_error("Preview asset GPU budget is 256 MiB");
        }
    }
};
AssetRenderer::AssetRenderer(const CompiledAssets& a):impl_(std::make_unique<Impl>()) { impl_->initialize(a); }
AssetRenderer::~AssetRenderer()=default;
void AssetRenderer::sync_surfaces(const std::map<std::string,DynamicSurface>& surfaces) {
    for(auto it=impl_->surfaces.begin();it!=impl_->surfaces.end();)if(!surfaces.contains(it->first)){if(bgfx::isValid(it->second.texture))bgfx::destroy(it->second.texture);it=impl_->surfaces.erase(it);}else++it;
    for(const auto&[id,source]:surfaces){auto& gpu=impl_->surfaces[id];
        if(source.width!=gpu.w||source.height!=gpu.h){
            if(bgfx::isValid(gpu.texture))bgfx::destroy(gpu.texture);
            gpu.texture=bgfx::createTexture2D(uint16_t(source.width),uint16_t(source.height),false,1,bgfx::TextureFormat::RGBA8,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
            if(!bgfx::isValid(gpu.texture))throw std::runtime_error("Cannot create material surface");
            gpu.w=source.width;gpu.h=source.height;gpu.revision=0;
        }
        if(gpu.revision!=source.revision){bgfx::updateTexture2D(gpu.texture,0,0,0,0,uint16_t(gpu.w),uint16_t(gpu.h),bgfx::copy(source.pixels.data(),uint32_t(source.pixels.size()*4)));gpu.revision=source.revision;}
    }
}
void AssetRenderer::draw(const std::vector<Entity>& entities,bgfx::ProgramHandle /*program*/,bool flat,const Vec3& eye) {
    constexpr float radians=3.14159265358979323846f/180.f;
    std::vector<const Entity*> sprites;
    for(const auto& e:entities) {
        if(!e.visible||e.asset.empty()) continue;
        auto mesh=impl_->meshes.find(e.asset);
        if(mesh==impl_->meshes.end()) { if(!impl_->models.contains(e.asset))sprites.push_back(&e); continue; }
        float transform[16];
        bx::mtxSRT(transform,e.size[0]*e.visual_scale[0],e.size[1]*e.visual_scale[1],e.size[2]*e.visual_scale[2],e.rotation[0]*radians,e.rotation[1]*radians,e.rotation[2]*radians,e.position[0],e.position[1],e.position[2]);
        // The shader derives normals from transformed surface derivatives;
        // positive Y lights the upper surfaces in this D3D11 view convention.
        for(const auto& part:mesh->second.parts) {
            float parameters[4][4]{{-.309f,.928f,.206f,3}, {1,1,1,0}, {.36f,.43f,.58f,0}, {1,1,1,1}};
            for(int channel=0;channel<3;++channel) parameters[3][channel]=std::clamp(part.color[channel]*e.color[channel],0.f,1.f);
            parameters[3][3]=std::clamp(part.color[3]*e.color[3],0.f,1.f);
            bgfx::setUniform(impl_->lighting,parameters,4);
            bgfx::setTransform(transform);
            bgfx::setVertexBuffer(0,mesh->second.vertices); bgfx::setIndexBuffer(mesh->second.indices,part.first,part.count);
            bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_LESS|BGFX_STATE_MSAA|(e.color[3]<1?BGFX_STATE_BLEND_ALPHA:BGFX_STATE_WRITE_Z));
            bgfx::submit(0,impl_->mesh_program);
        }
    }
    struct DrawModel { const Entity* entity; const ModelAsset* asset; std::size_t primitive; bool transparent; double distance; };
    std::vector<DrawModel> model_draws;
    std::map<std::string,ModelPose> poses;
    for(const auto& e:entities) {
        auto found=impl_->assets.models.find(e.asset);
        if(!e.visible||e.color[3]<=0||found==impl_->assets.models.end())continue;
        auto& cached=impl_->samplers[e.id];
        if(cached.asset!=found->second.get()){cached.asset=found->second.get();cached.value=std::make_unique<ModelSampler>(*cached.asset);}
        poses.emplace(e.id,cached.value->sample(e.animation,e.animation_time,e.animation_previous,e.animation_previous_time,e.animation_blend));
        double squared=0;for(int k=0;k<3;++k){double d=e.position[k]-eye[k];squared+=d*d;}
        for(std::size_t p=0;p<cached.asset->primitives.size();++p){const auto&mat=cached.asset->materials[cached.asset->primitives[p].material];model_draws.push_back({&e,cached.asset,p,mat.alpha_mode=="BLEND"||e.color[3]<1,squared});}
    }
    for(auto it=impl_->samplers.begin();it!=impl_->samplers.end();)if(!poses.contains(it->first))it=impl_->samplers.erase(it);else++it;
    std::stable_sort(model_draws.begin(),model_draws.end(),[](const auto&a,const auto&b){if(a.transparent!=b.transparent)return !a.transparent;return a.transparent&&a.distance>b.distance;});
    for(const auto& draw:model_draws) {
        const auto&e=*draw.entity;const auto&primitive=draw.asset->primitives[draw.primitive];const auto&mat=draw.asset->materials[primitive.material];
        auto vertices=deform_model_primitive(primitive,poses.at(e.id));
        bgfx::TransientVertexBuffer buffer;
        if(bgfx::getAvailTransientVertexBuffer(uint32_t(vertices.size()),impl_->model_layout)<vertices.size())throw std::runtime_error("Model CPU skinning transient vertex budget exceeded; reduce visible model vertices");
        bgfx::allocTransientVertexBuffer(&buffer,uint32_t(vertices.size()),impl_->model_layout);
        auto*out=reinterpret_cast<SkinnedVertex*>(buffer.data);
        float transform[16],rotation[16];
        const Vec3 scale{e.size[0]*e.visual_scale[0],e.size[1]*e.visual_scale[1],e.size[2]*e.visual_scale[2]};
        bx::mtxSRT(transform,scale[0],scale[1],scale[2],e.rotation[0]*radians,e.rotation[1]*radians,e.rotation[2]*radians,e.position[0],e.position[1],e.position[2]);
        bx::mtxSRT(rotation,1,1,1,e.rotation[0]*radians,e.rotation[1]*radians,e.rotation[2]*radians,0,0,0);
        for(std::size_t i=0;i<vertices.size();++i) {
            const auto&v=vertices[i];float p[3],n[3];
            for(int r=0;r<3;++r){p[r]=transform[r]*v.position[0]+transform[4+r]*v.position[1]+transform[8+r]*v.position[2]+transform[12+r];n[r]=rotation[r]*v.normal[0]/scale[0]+rotation[4+r]*v.normal[1]/scale[1]+rotation[8+r]*v.normal[2]/scale[2];}
            Color tint;for(int c=0;c<4;++c)tint[c]=v.color[c]*mat.color[c]*e.color[c];
            out[i]={p[0],p[1],p[2],n[0],n[1],n[2],color(tint),v.uv[0],v.uv[1]};
        }
        const auto&gpu=impl_->models.at(e.asset);
        float params[4]{mat.unlit?1.f:0.f,mat.alpha_mode=="MASK"?mat.alpha_cutoff:-1.f,mat.alpha_mode=="OPAQUE"&&e.color[3]>=1?1.f:0.f,0};
        bgfx::setUniform(impl_->model_material,params);
        bgfx::setVertexBuffer(0,&buffer);bgfx::setIndexBuffer(gpu.indices[draw.primitive]);
        bgfx::setTexture(0,impl_->sampler,mat.image<0?impl_->white:gpu.textures.at(mat.image));
        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_LEQUAL|BGFX_STATE_MSAA|(draw.transparent?BGFX_STATE_BLEND_ALPHA:BGFX_STATE_WRITE_Z));
        bgfx::submit(0,impl_->model_program);
    }
    if(flat) std::stable_sort(sprites.begin(),sprites.end(),[](auto a,auto b){return a->position[2]<b->position[2];});
    else std::stable_sort(sprites.begin(),sprites.end(),[&](auto a,auto b){
        auto distance=[&](const Entity* e){ double squared=0; for(int axis=0;axis<3;++axis) { const double delta=e->position[axis]-eye[axis]; squared+=delta*delta; } return squared; };
        return distance(a)>distance(b);
    });
    std::vector<SpriteVertex> batch_vertices;
    std::vector<uint16_t> batch_indices;
    bgfx::TextureHandle batch_texture=BGFX_INVALID_HANDLE;
    auto flush=[&]() {
        if(batch_vertices.empty()) return;
        bgfx::TransientVertexBuffer vb;bgfx::TransientIndexBuffer ib;
        if(!bgfx::allocTransientBuffers(&vb,impl_->sprite_layout,uint32_t(batch_vertices.size()),&ib,uint32_t(batch_indices.size())))
            throw std::runtime_error("Sprite transient buffer budget exceeded");
        std::memcpy(vb.data,batch_vertices.data(),batch_vertices.size()*sizeof(SpriteVertex));
        std::memcpy(ib.data,batch_indices.data(),batch_indices.size()*sizeof(uint16_t));
        float identity[16];bx::mtxIdentity(identity);bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0,&vb);bgfx::setIndexBuffer(&ib);
        bgfx::setTexture(0,impl_->sampler,batch_texture);
        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_LEQUAL|BGFX_STATE_BLEND_ALPHA|BGFX_STATE_MSAA);
        bgfx::submit(0,impl_->sprite_program);
        batch_vertices.clear();batch_indices.clear();
    };
    for(auto ptr:sprites) {
        const auto& e=*ptr;
        auto sprite=impl_->assets.sprites.find(e.asset);
        if(sprite==impl_->assets.sprites.end()) continue;
        size_t frame=sprite_frame_at(*sprite->second,e.animation,e.animation_time);
        auto region=impl_->textures.at(e.asset).at(frame);
        if(auto surface=impl_->surfaces.find(e.id);surface!=impl_->surfaces.end())region={surface->second.texture,0,0,1,1};
        // Consecutive runs only: preserve stable depth/alpha ordering, including
        // overlapping sprites. Limit indices to the 16-bit transient format.
        if(region.texture.idx!=batch_texture.idx||batch_vertices.size()+4>65532) flush();
        batch_texture=region.texture;
        float transform[16];
        // Box2D and authored 2D polygons use counterclockwise positive angles.
        // bx's SRT Z convention is clockwise; previews and body sprites must
        // share the physics convention rather than counter-rotate on contact.
        const float sprite_z = (flat ? -e.rotation[2] : e.rotation[2]) * radians;
        bx::mtxSRT(transform,e.size[0]*e.visual_scale[0],e.size[1]*e.visual_scale[1],e.visual_scale[2],e.rotation[0]*radians,e.rotation[1]*radians,sprite_z,e.position[0],e.position[1],e.position[2]);
        const float left=e.flip_x?region.u1:region.u0,right=e.flip_x?region.u0:region.u1;
        const uint32_t tint=color(e.color);
        SpriteVertex vertices[]{{-.5f,.5f,0,tint,left,region.v0},{.5f,.5f,0,tint,right,region.v0},{.5f,-.5f,0,tint,right,region.v1},{-.5f,-.5f,0,tint,left,region.v1}};
        const uint16_t indices[]{0,1,2,0,2,3};
        const auto base=uint16_t(batch_vertices.size());
        for(auto v:vertices) {
            const float x=v.x,y=v.y,z=v.z;
            v.x=transform[0]*x+transform[4]*y+transform[8]*z+transform[12];
            v.y=transform[1]*x+transform[5]*y+transform[9]*z+transform[13];
            v.z=transform[2]*x+transform[6]*y+transform[10]*z+transform[14];
            batch_vertices.push_back(v);
        }
        for(auto index:indices) batch_indices.push_back(base+index);
    }
    flush();
}
}
