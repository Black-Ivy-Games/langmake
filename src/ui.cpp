#include "langmake/ui.hpp"
#include "langmake/platform_shaders.hpp"
#include <SDL3/SDL.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <lodepng/lodepng.h>
#include <vs_debugdraw_fill_texture.bin.h>
#include <fs_debugdraw_fill_texture.bin.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <sstream>
#include <iomanip>
#include <functional>
#include <set>
#include <stdexcept>

namespace lm {
namespace {
struct MemoryFiles final : Rml::FileInterface {
    struct OpenFile { const std::string* bytes; std::size_t position=0; };
    std::map<std::string,std::string> sources;
    std::vector<std::string> missing;
    std::uint64_t missing_count=0;
    std::string last_missing;
    Rml::FileHandle Open(const Rml::String& path) override {
        auto found=sources.find(path);
        if(found==sources.end()){++missing_count;last_missing=path;if(missing.size()<128)missing.push_back(path);return 0;}
        return reinterpret_cast<Rml::FileHandle>(new OpenFile{&found->second,0});
    }
    void Close(Rml::FileHandle file)override{delete reinterpret_cast<OpenFile*>(file);}
    size_t Read(void* buffer,size_t size,Rml::FileHandle file)override {
        auto& opened=*reinterpret_cast<OpenFile*>(file);size=std::min(size,opened.bytes->size()-opened.position);
        std::memcpy(buffer,opened.bytes->data()+opened.position,size);opened.position+=size;return size;
    }
    bool Seek(Rml::FileHandle file,long offset,int origin)override {
        auto& opened=*reinterpret_cast<OpenFile*>(file);
        const auto base=origin==SEEK_SET?0LL:origin==SEEK_CUR?static_cast<long long>(opened.position):origin==SEEK_END?static_cast<long long>(opened.bytes->size()):-1LL;
        const auto position=base+offset;if(base<0||position<0||static_cast<unsigned long long>(position)>opened.bytes->size())return false;
        opened.position=std::size_t(position);return true;
    }
    size_t Tell(Rml::FileHandle file)override{return reinterpret_cast<OpenFile*>(file)->position;}
    size_t Length(Rml::FileHandle file)override{return reinterpret_cast<OpenFile*>(file)->bytes->size();}
};
struct System final : Rml::SystemInterface {
    double time=0;
    std::vector<std::string> errors;
    std::uint64_t error_count=0;
    std::string last_error;
    double GetElapsedTime()override{return time;}
    bool LogMessage(Rml::Log::Type type,const Rml::String& message)override {
        if(type==Rml::Log::LT_ERROR || type==Rml::Log::LT_ASSERT){++error_count;last_error=message;if(errors.size()<128)errors.push_back(message);}return true;
    }
    void JoinPath(Rml::String& output,const Rml::String& document,const Rml::String& path)override {
        if(path.find(':')!=std::string::npos || path.find('\\')!=std::string::npos || path.starts_with('/')){output="__forbidden_ui_resource__";return;}
        output=(std::filesystem::path(document).parent_path()/path).lexically_normal().generic_string();
    }
    void ActivateKeyboard(Rml::Vector2f,float)override{if(auto* window=SDL_GetKeyboardFocus())SDL_StartTextInput(window);}
    void DeactivateKeyboard()override{if(auto* window=SDL_GetKeyboardFocus())SDL_StopTextInput(window);}
};
struct UiRuntime {
    MemoryFiles files;
    System system;
    std::map<std::string,std::string> font_aliases;
    std::size_t font_bytes=0;
    explicit UiRuntime(const std::filesystem::path& executable) {
        const auto path=executable/"fonts/LatoLatin-Regular.ttf";
        std::ifstream stream(path,std::ios::binary|std::ios::ate);
        if(!stream || stream.tellg()<=0 || stream.tellg()>4*1024*1024)throw std::runtime_error("Missing or oversized bundled UI font: "+path.string());
        std::string bytes(std::size_t(stream.tellg()),'\0');stream.seekg(0);stream.read(bytes.data(),std::streamsize(bytes.size()));
        if(!stream)throw std::runtime_error("Could not read bundled UI font");
        files.sources.emplace("__langmake__/default.ttf",std::move(bytes));
        Rml::SetFileInterface(&files);Rml::SetSystemInterface(&system);
        if(!Rml::Initialise())throw std::runtime_error("Could not initialize RmlUi");
        if(!Rml::LoadFontFace("__langmake__/default.ttf","Langmake",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true)) {
            Rml::Shutdown();throw std::runtime_error("Could not load bundled UI font");
        }
    }
    ~UiRuntime(){Rml::Shutdown();Rml::SetFileInterface(nullptr);Rml::SetSystemInterface(nullptr);}
};
std::weak_ptr<UiRuntime> active_runtime;
std::uint64_t next_context=0;
std::shared_ptr<UiRuntime> runtime(const std::filesystem::path& executable) {
    auto result=active_runtime.lock();if(!result){result=std::make_shared<UiRuntime>(executable);active_runtime=result;}return result;
}
struct UiVertex {float x,y,z;uint32_t color;float u,v;};
struct Renderer final : Rml::RenderInterface {
    struct Geometry {std::vector<UiVertex> vertices;std::vector<uint32_t> indices;};
    struct Texture {bgfx::TextureHandle handle=BGFX_INVALID_HANDLE;std::size_t bytes=0;};
    std::shared_ptr<UiRuntime> runtime;
    bool headless=false,scissor_enabled=false;
    int width=1280,height=800;
    std::array<int,4> safe{0,0,1280,800};
    std::size_t geometry_bytes=0,texture_bytes=0,texture_count=0;
    Rml::Rectanglei scissor=Rml::Rectanglei::FromSize({1280,800});
    float transform[16];
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler=BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white=BGFX_INVALID_HANDLE;
    Renderer(std::shared_ptr<UiRuntime> owner,bool cpu):runtime(std::move(owner)),headless(cpu) {
        bx::mtxIdentity(transform);
        if(headless)return;
        layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
        auto vs=bgfx::createShader(bgfx::copy(LM_SHADER(vs_debugdraw_fill_texture),sizeof(LM_SHADER(vs_debugdraw_fill_texture))));
        auto fs=bgfx::createShader(bgfx::copy(LM_SHADER(fs_debugdraw_fill_texture),sizeof(LM_SHADER(fs_debugdraw_fill_texture))));
        if(!bgfx::isValid(vs)||!bgfx::isValid(fs)){if(bgfx::isValid(vs))bgfx::destroy(vs);if(bgfx::isValid(fs))bgfx::destroy(fs);throw std::runtime_error("Cannot create UI shaders");}
        program=bgfx::createProgram(vs,fs,true);sampler=bgfx::createUniform("s_texColor",bgfx::UniformType::Sampler);
        const uint32_t pixel=0xffffffff;white=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(&pixel,4));
        if(!bgfx::isValid(program)||!bgfx::isValid(sampler)||!bgfx::isValid(white)){if(bgfx::isValid(white))bgfx::destroy(white);if(bgfx::isValid(sampler))bgfx::destroy(sampler);if(bgfx::isValid(program))bgfx::destroy(program);throw std::runtime_error("Cannot create UI renderer resources");}
    }
    ~Renderer(){if(headless)return;if(bgfx::isValid(white))bgfx::destroy(white);if(bgfx::isValid(sampler))bgfx::destroy(sampler);if(bgfx::isValid(program))bgfx::destroy(program);}
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,Rml::Span<const int> indices)override {
        const auto bytes=vertices.size()*sizeof(UiVertex)+indices.size()*sizeof(uint32_t);
        if(vertices.size()>250000 || indices.size()>750000 || geometry_bytes+bytes>64*1024*1024)throw std::runtime_error("UI exceeds geometry budget");
        auto geometry=std::make_unique<Geometry>();geometry->vertices.reserve(vertices.size());geometry->indices.reserve(indices.size());
        for(const auto& v:vertices)geometry->vertices.push_back({v.position.x,v.position.y,0,uint32_t(v.colour.red)|(uint32_t(v.colour.green)<<8)|(uint32_t(v.colour.blue)<<16)|(uint32_t(v.colour.alpha)<<24),v.tex_coord.x,v.tex_coord.y});
        for(const int index:indices){if(index<0 || std::size_t(index)>=vertices.size())throw std::runtime_error("UI geometry index out of bounds");geometry->indices.push_back(uint32_t(index));}
        geometry_bytes+=bytes;return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle)override {
        auto* geometry=reinterpret_cast<Geometry*>(handle);geometry_bytes-=geometry->vertices.size()*sizeof(UiVertex)+geometry->indices.size()*sizeof(uint32_t);delete geometry;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle handle,Rml::Vector2f translation,Rml::TextureHandle texture)override {
        if(headless)return;const auto& geometry=*reinterpret_cast<Geometry*>(handle);
        if(geometry.vertices.empty()||geometry.indices.empty())return;
        bgfx::TransientVertexBuffer vertices;bgfx::TransientIndexBuffer indices;
        if(!bgfx::allocTransientBuffers(&vertices,layout,uint32_t(geometry.vertices.size()),&indices,uint32_t(geometry.indices.size()),true))throw std::runtime_error("UI transient geometry budget exhausted");
        std::memcpy(vertices.data,geometry.vertices.data(),geometry.vertices.size()*sizeof(UiVertex));std::memcpy(indices.data,geometry.indices.data(),geometry.indices.size()*sizeof(uint32_t));
        float translation_matrix[16],model[16];bx::mtxTranslate(translation_matrix,translation.x,translation.y,0);bx::mtxMul(model,translation_matrix,transform);
        model[12]+=safe[0];model[13]+=safe[1];
        bgfx::setTransform(model);bgfx::setVertexBuffer(0,&vertices);bgfx::setIndexBuffer(&indices);
        bgfx::setTexture(0,sampler,texture?reinterpret_cast<Texture*>(texture)->handle:white);
        const int left=safe[0]+(scissor_enabled?std::clamp(scissor.Left(),0,safe[2]):0),top=safe[1]+(scissor_enabled?std::clamp(scissor.Top(),0,safe[3]):0);
        const int right=safe[0]+(scissor_enabled?std::clamp(scissor.Right(),left-safe[0],safe[2]):safe[2]),bottom=safe[1]+(scissor_enabled?std::clamp(scissor.Bottom(),top-safe[1],safe[3]):safe[3]);
        if(left==right||top==bottom)return;bgfx::setScissor(uint16_t(left),uint16_t(top),uint16_t(right-left),uint16_t(bottom-top));
        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_MSAA|BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));
        bgfx::submit(1,program);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels,Rml::Vector2i dimensions)override {
        if(dimensions.x<1||dimensions.y<1||dimensions.x>4096||dimensions.y>4096||pixels.size()!=std::size_t(dimensions.x)*dimensions.y*4||texture_bytes+pixels.size()>64*1024*1024||texture_count>=2048)throw std::runtime_error("UI texture budget exceeded");
        auto texture=std::make_unique<Texture>();texture->bytes=pixels.size();
        if(!headless){texture->handle=bgfx::createTexture2D(uint16_t(dimensions.x),uint16_t(dimensions.y),false,1,bgfx::TextureFormat::RGBA8,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,bgfx::copy(pixels.data(),uint32_t(pixels.size())));if(!bgfx::isValid(texture->handle))throw std::runtime_error("Cannot create UI texture");}
        texture_bytes+=pixels.size();++texture_count;return reinterpret_cast<Rml::TextureHandle>(texture.release());
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions,const Rml::String& path)override {
        const auto found=runtime->files.sources.find(path);if(found==runtime->files.sources.end())throw std::runtime_error("UI image is not registered: "+path);
        const auto& bytes=found->second;unsigned width=0,height=0;lodepng::State state;
        if(auto error=lodepng_inspect(&width,&height,&state,reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size()))throw std::runtime_error(std::string("Invalid UI PNG: ")+lodepng_error_text(error));
        if(width>4096||height>4096||std::uint64_t(width)*height*4>64*1024*1024)throw std::runtime_error("UI PNG exceeds pixel budget");
        std::vector<unsigned char> pixels;auto error=lodepng::decode(pixels,width,height,reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size());if(error)throw std::runtime_error(std::string("Invalid UI PNG: ")+lodepng_error_text(error));
        for(std::size_t i=0;i<pixels.size();i+=4)for(int c=0;c<3;++c)pixels[i+c]=uint8_t(unsigned(pixels[i+c])*pixels[i+3]/255);
        dimensions={int(width),int(height)};return GenerateTexture({pixels.data(),pixels.size()},dimensions);
    }
    void ReleaseTexture(Rml::TextureHandle handle)override{auto* texture=reinterpret_cast<Texture*>(handle);texture_bytes-=texture->bytes;--texture_count;if(!headless&&bgfx::isValid(texture->handle))bgfx::destroy(texture->handle);delete texture;}
    void EnableScissorRegion(bool enabled)override{scissor_enabled=enabled;}
    void SetScissorRegion(Rml::Rectanglei value)override{scissor=value;}
    void SetTransform(const Rml::Matrix4f* value)override{if(!value)bx::mtxIdentity(transform);else for(int col=0;col<4;++col)for(int row=0;row<4;++row)transform[col*4+row]=value->GetColumn(col)[row];}
    void begin(int w,int h,std::array<int,4> content){width=w;height=h;safe=content;if(headless)return;float projection[16];bx::mtxOrtho(projection,0,float(w),float(h),0,0,1000,0,bgfx::getCaps()->homogeneousDepth);bgfx::setViewRect(1,0,0,uint16_t(w),uint16_t(h));bgfx::setViewMode(1,bgfx::ViewMode::Sequential);bgfx::setViewClear(1,BGFX_CLEAR_NONE);bgfx::setViewTransform(1,nullptr,projection);bgfx::touch(1);}
};
std::string escaped(const std::string& value){std::string result;for(char c:value){if(c=='&')result+="&amp;";else if(c=='<')result+="&lt;";else if(c=='>')result+="&gt;";else result+=c;}return result;}
std::string text(const Json& value){return value.is_string()?value.get<std::string>():value.dump();}
bool interactive(Rml::Element* element){return element && (element->HasAttribute("data-event")||element->HasAttribute("data-action")||element->GetTagName()=="button"||element->GetTagName()=="input"||element->GetTagName()=="select"||element->GetTagName()=="textarea");}
int modifiers(){const auto keys=SDL_GetModState();return ((keys&SDL_KMOD_SHIFT)?Rml::Input::KM_SHIFT:0)|((keys&SDL_KMOD_CTRL)?Rml::Input::KM_CTRL:0)|((keys&SDL_KMOD_ALT)?Rml::Input::KM_ALT:0);}
Rml::Input::KeyIdentifier key(SDL_Keycode value){
    using namespace Rml::Input;
    if(value>=SDLK_A&&value<=SDLK_Z)return KeyIdentifier(KI_A+value-SDLK_A);
    if(value>=SDLK_0&&value<=SDLK_9)return KeyIdentifier(KI_0+value-SDLK_0);
    switch(value){case SDLK_TAB:return KI_TAB;case SDLK_RETURN:case SDLK_KP_ENTER:return KI_RETURN;case SDLK_SPACE:return KI_SPACE;case SDLK_ESCAPE:case SDLK_AC_BACK:return KI_ESCAPE;case SDLK_LEFT:return KI_LEFT;case SDLK_RIGHT:return KI_RIGHT;case SDLK_UP:return KI_UP;case SDLK_DOWN:return KI_DOWN;case SDLK_BACKSPACE:return KI_BACK;case SDLK_DELETE:return KI_DELETE;case SDLK_HOME:return KI_HOME;case SDLK_END:return KI_END;case SDLK_PAGEUP:return KI_PRIOR;case SDLK_PAGEDOWN:return KI_NEXT;default:return KI_UNKNOWN;}
}
}
struct UiRenderer::Impl final : Rml::EventListener {
    std::shared_ptr<UiRuntime> runtime;
    std::unique_ptr<Renderer> renderer;
    Rml::Context* context=nullptr;
    std::string name;
    std::vector<Rml::ElementDocument*> documents;
    std::map<std::string,Rml::Element*> elements;
    std::vector<Rml::Element*> focusables;
    std::map<std::string,std::string> display;
    Json previous=Json::object(),events=Json::array();
    std::vector<std::string> source_keys;
    int width=1280,height=800;
    std::array<int,4> requested_safe{},safe{0,0,1280,800};
    float density=1;
    std::size_t nodes=0;
    bool enabled=false;
    struct Held {Rml::Element* element=nullptr;float x=0,y=0;bool active=true;bool positional=true;};
    std::map<std::int64_t,Held> pointers;
    struct ScrollDrag {Rml::Element* element;float y,top;};
    std::map<std::int64_t,ScrollDrag> scroll_drags;
    std::set<Rml::Element*> pulses;
    std::set<std::string> actions;
    std::map<std::string,std::string> font_sources;
    std::string expected_characters;
    float mouse_x=0,mouse_y=0;
    Impl(const Project& project,const std::filesystem::path& executable,bool headless) {
        if(!project.ui.contains("documents") || project.ui["documents"].empty())return;
        enabled=true;runtime=lm::runtime(executable);
        actions={"fire","jump"};
        if(project.document.contains("input")&&project.document["input"].contains("actions"))for(const auto&[id,binding]:project.document["input"]["actions"].items())actions.insert(id);
        expected_characters=project.ui.value("expected_characters",std::string());
        font_sources["Langmake"]=runtime->files.sources.at("__langmake__/default.ttf");
        name="langmake-ui-"+std::to_string(++next_context);
        const auto missing_start=runtime->files.missing_count,errors_start=runtime->system.error_count;
        try {
            for(const char* kind:{"documents","styles","fonts","images"})if(project.ui.contains(kind))for(const auto& [id,path]:project.ui[kind].items()) {
                const auto source=path.get<std::string>();const auto found=project.sources.find(source);if(found==project.sources.end())throw std::runtime_error("UI source is not in project source graph: "+source);
                const auto key=name+"/"+source;if(!runtime->files.sources.contains(key)){runtime->files.sources.emplace(key,found->second);source_keys.push_back(key);}
            }
            renderer=std::make_unique<Renderer>(runtime,headless);context=Rml::CreateContext(name,{width,height},renderer.get());if(!context)throw std::runtime_error("Cannot create UI context");
            context->GetRootElement()->SetProperty("font-family","Langmake");context->GetRootElement()->SetProperty("font-size","20dp");
            std::map<std::string,std::string> font_names;
            if(project.ui.contains("fonts"))for(const auto& [id,path]:project.ui["fonts"].items()) {
                const auto& bytes=project.sources.at(path.get<std::string>());
                std::uint64_t hash=14695981039346656037ULL;for(unsigned char byte:bytes){hash^=byte;hash*=1099511628211ULL;}
                const auto key=std::to_string(hash);
                if(!runtime->font_aliases.contains(key)) {
                    if(runtime->font_aliases.size()>=128 || runtime->font_bytes+bytes.size()>64*1024*1024)throw std::runtime_error("UI font cache budget exceeded; restart preview to release unused font revisions");
                    const auto family="langmake-font-"+key;
                    if(!Rml::LoadFontFace(name+"/"+path.get<std::string>(),family,Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal))throw std::runtime_error("Cannot load registered UI font: "+id);
                    runtime->font_aliases.emplace(key,family);runtime->font_bytes+=bytes.size();
                }
                font_names.emplace(id,runtime->font_aliases.at(key));font_sources[runtime->font_aliases.at(key)]=bytes;
            }
            for(const auto& [id,path]:project.ui["documents"].items()) {
                auto* document=context->LoadDocument(name+"/"+path.get<std::string>());if(!document)throw std::runtime_error("Cannot load UI document: "+id);
                document->SetProperty("pointer-events","none");
                document->Show(Rml::ModalFlag::None,Rml::FocusFlag::None);documents.push_back(document);
                walk(document,0);
                apply_fonts(document,font_names);
            }
            context->AddEventListener("click",this);context->AddEventListener("change",this);
            context->Update();
            // Trigger lazy font/image decoding during CPU preflight so a bad
            // source is rejected before the last valid preview is replaced.
            if(headless)context->Render();
            if(runtime->files.missing_count>missing_start)throw std::runtime_error("UI reference must be registered: "+runtime->files.last_missing);
            if(runtime->system.error_count>errors_start)throw std::runtime_error("UI document error: "+runtime->system.last_error);
        } catch(...) {close();throw;}
    }
    void close(){if(context){Rml::RemoveContext(name);context=nullptr;Rml::ReleaseRenderManagers();}renderer.reset();if(runtime)for(const auto& key:source_keys)runtime->files.sources.erase(key);source_keys.clear();}
    ~Impl(){close();}
    void walk(Rml::Element* element,int depth) {
        if(depth>64 || ++nodes>4096)throw std::runtime_error("UI exceeds element/depth budget");
        const auto id=element->GetId();if(!id.empty()) {
            if(!elements.emplace(id,element).second)throw std::runtime_error("Duplicate UI element ID: "+id);
        }
        if(element->GetTagName()=="script")throw std::runtime_error("UI behavior belongs in Luau; embedded scripts are unsupported");
        if(element->HasAttribute("data-action")&&id.empty())throw std::runtime_error("UI data-action requires a stable element id");
        if(element->HasAttribute("data-hit-padding")){const auto value=element->GetAttribute<Rml::String>("data-hit-padding","");std::size_t used=0;float amount=0;try{amount=std::stof(value,&used);}catch(...){throw std::runtime_error("UI data-hit-padding must be a number in dp");}if(used!=value.size()||!std::isfinite(amount)||amount<0||amount>256)throw std::runtime_error("UI data-hit-padding must be within 0..256 dp");}
        if(element->HasAttribute("data-drag-scroll"))element->SetProperty("pointer-events","auto");
        if(interactive(element)){element->SetProperty("pointer-events","auto");element->SetProperty("tab-index","auto");focusables.push_back(element);}
        for(int i=0;i<element->GetNumChildren();++i)walk(element->GetChild(i),depth+1);
    }
    void apply_fonts(Rml::Element* element,const std::map<std::string,std::string>& names) {
        const auto* property=element->GetProperty("font-family");
        if(property){const auto name=property->Get<Rml::String>();auto found=names.find(name);if(found!=names.end())element->SetProperty("font-family",found->second);}
        for(int i=0;i<element->GetNumChildren();++i)apply_fonts(element->GetChild(i),names);
    }
    void remove_descendant_ids(Rml::Element* element){for(int i=0;i<element->GetNumChildren();++i){auto* child=element->GetChild(i);cancel_element(child);elements.erase(child->GetId());std::erase(focusables,child);remove_descendant_ids(child);}}
    void set_text(Rml::Element* element,const Json& value){remove_descendant_ids(element);element->SetInnerRML(escaped(text(value)));}
    void ProcessEvent(Rml::Event& event)override {
        auto* element=event.GetTargetElement();while(element && !interactive(element))element=element->GetParentNode();
        if(!element || element->GetId().empty())return;
        if(!element->HasAttribute("data-action")&&event.GetType()=="click")reset_input();
        if(element->HasAttribute("data-action")&&!element->HasAttribute("data-event"))return;
        if(events.size()<256)events.push_back({{"type","ui"},{"id",element->GetId()},{"event",element->GetAttribute<Rml::String>("data-event",event.GetType())},{"value",event.GetParameter<Rml::String>("value",element->GetAttribute<Rml::String>("value",""))}});
        event.StopPropagation();
    }
    std::array<float,4> bounds(Rml::Element* element,bool expanded=false)const {
        const auto offset=element->GetAbsoluteOffset(Rml::BoxArea::Border),size=element->GetBox().GetSize(Rml::BoxArea::Border);
        const float padding=expanded?element->GetAttribute<float>("data-hit-padding",0)*density:0;
        return {offset.x+safe[0]-padding,offset.y+safe[1]-padding,size.x+2*padding,size.y+2*padding};
    }
    std::array<float,4> hit_bounds(Rml::Element* element)const {
        auto r=bounds(element,true);
        auto clip=[&](std::array<float,4> c,bool x,bool y){const float right=std::min(r[0]+r[2],c[0]+c[2]),bottom=std::min(r[1]+r[3],c[1]+c[3]);if(x){r[0]=std::max(r[0],c[0]);r[2]=std::max(0.f,right-r[0]);}if(y){r[1]=std::max(r[1],c[1]);r[3]=std::max(0.f,bottom-r[1]);}};
        clip({float(safe[0]),float(safe[1]),float(safe[2]),float(safe[3])},true,true);
        for(auto* p=element->GetParentNode();p;p=p->GetParentNode()){const auto& style=p->GetComputedValues();clip(bounds(p),style.overflow_x()!=Rml::Style::Overflow::Visible,style.overflow_y()!=Rml::Style::Overflow::Visible);}
        return r;
    }
    bool available(Rml::Element* element)const {
        if(!element||!element->IsVisible(true))return false;
        if(element->GetComputedValues().pointer_events()==Rml::Style::PointerEvents::None)return false;
        for(auto* parent=element;parent;parent=parent->GetParentNode())if(parent->HasAttribute("disabled"))return false;
        return true;
    }
    bool inside(Rml::Element* element,float x,float y)const {
        if(!available(element)||x<safe[0]||y<safe[1]||x>=safe[0]+safe[2]||y>=safe[1]+safe[3])return false;
        auto r=bounds(element,true);if(x<r[0]||y<r[1]||x>=r[0]+r[2]||y>=r[1]+r[3])return false;
        for(auto* p=element->GetParentNode();p;p=p->GetParentNode()){
            const auto& style=p->GetComputedValues();r=bounds(p);
            if(style.overflow_x()!=Rml::Style::Overflow::Visible&&(x<r[0]||x>=r[0]+r[2]))return false;
            if(style.overflow_y()!=Rml::Style::Overflow::Visible&&(y<r[1]||y>=r[1]+r[3]))return false;
        }
        return true;
    }
    Rml::Element* action_at(float x,float y)const {
        if(!context)return nullptr;
        auto* hit=context->GetElementAtPoint({x-safe[0],y-safe[1]});
        for(auto* p=hit;p;p=p->GetParentNode())if(interactive(p))return p->HasAttribute("data-action")&&inside(p,x,y)?p:nullptr;
        // Expanded hit regions are a fallback only: never steal another actual
        // interactive element's pixels. Later authored controls win padding ties.
        for(auto i=focusables.rbegin();i!=focusables.rend();++i)if((*i)->HasAttribute("data-action")&&inside(*i,x,y))return *i;
        return nullptr;
    }
    bool held(Rml::Element* element)const {for(const auto&[id,p]:pointers)if(p.element==element&&p.active)return true;return false;}
    void emit_action(Rml::Element* element,const char* phase){if(events.size()<256)events.push_back({{"type","ui"},{"id",element->GetId()},{"event",phase},{"action",element->GetAttribute<Rml::String>("data-action","")},{"value",""}});}
    void refresh_active(Rml::Element* element){element->SetPseudoClass("active",held(element));}
    void cancel_element(Rml::Element* element){
        std::erase_if(scroll_drags,[&](const auto& entry){return entry.second.element==element;});
        bool canceled=pulses.erase(element)>0;
        for(auto i=pointers.begin();i!=pointers.end();)if(i->second.element==element){canceled=true;i=pointers.erase(i);}else ++i;
        if(canceled){emit_action(element,"cancel");element->SetPseudoClass("active",false);}
    }
    void prune(){std::vector<Rml::Element*> canceled;for(const auto&[id,p]:pointers)if(!available(p.element)||(!p.positional&&context->GetFocusElement()!=p.element))canceled.push_back(p.element);for(auto* e:pulses)if(!available(e))canceled.push_back(e);for(auto* e:canceled)cancel_element(e);}
    bool pointer(std::int64_t id,UiRenderer::PointerPhase phase,float x,float y,Rml::Element* explicit_element=nullptr){
        if(!std::isfinite(x)||!std::isfinite(y))return false;
        std::erase_if(scroll_drags,[&](const auto& entry){return !available(entry.second.element);});
        if(auto drag=scroll_drags.find(id);drag!=scroll_drags.end()){
            if(phase==UiRenderer::PointerPhase::move||phase==UiRenderer::PointerPhase::up){
                const float delta=y-drag->second.y;
                if(std::abs(delta)>=4*density)drag->second.element->SetScrollTop(drag->second.top-delta);
            }
            if(phase==UiRenderer::PointerPhase::up||phase==UiRenderer::PointerPhase::cancel)scroll_drags.erase(drag);
            return true;
        }
        if(phase==UiRenderer::PointerPhase::down&&!explicit_element&&context&&scroll_drags.size()<16){
            auto* hit=context->GetElementAtPoint({x-safe[0],y-safe[1]});
            for(auto* p=hit;p;p=p->GetParentNode()){
                const auto& tag=p->GetTagName();
                // Preserve controls and the native scrollbar's own dragging.
                if(interactive(p)||tag=="scrollbarvertical"||tag=="scrollbarhorizontal"||tag=="sliderbar"||tag=="slidertrack"||tag=="sliderarrowinc"||tag=="sliderarrowdec")break;
                if(p->HasAttribute("data-drag-scroll")&&inside(p,x,y)){
                    bool owned=false;for(const auto& [key,drag]:scroll_drags)if(drag.element==p)owned=true;
                    if(owned)return true;
                    scroll_drags.emplace(id,ScrollDrag{p,y,p->GetScrollTop()});return true;
                }
            }
        }
        prune();auto found=pointers.find(id);
        if(phase==UiRenderer::PointerPhase::down){
            if(found!=pointers.end())return true;
            auto* element=explicit_element?explicit_element:action_at(x,y);
            if(!element||!available(element)||!element->HasAttribute("data-action")||pointers.size()>=32)return false;
            pointers.emplace(id,Held{element,x,y,true,explicit_element==nullptr});pulses.insert(element);element->Focus(true);refresh_active(element);emit_action(element,"press");return true;
        }
        if(found==pointers.end())return false;
        auto* element=found->second.element;const bool was=found->second.active;
        if(phase==UiRenderer::PointerPhase::move){
            found->second.x=x;found->second.y=y;found->second.active=inside(element,x,y);
            if(was!=found->second.active){if(found->second.active){pulses.insert(element);emit_action(element,"press");}else{pulses.erase(element);emit_action(element,"release");}}refresh_active(element);return true;
        }
        const bool clicked=phase==UiRenderer::PointerPhase::up&&was&&(!found->second.positional||inside(element,x,y));
        pointers.erase(found);if(phase==UiRenderer::PointerPhase::cancel||!clicked)pulses.erase(element);
        refresh_active(element);if(phase==UiRenderer::PointerPhase::cancel||was)emit_action(element,phase==UiRenderer::PointerPhase::cancel?"cancel":"release");
        // Binding an action never also fires legacy click by accident. Games may
        // explicitly opt into a separate semantic click using data-event.
        if(clicked&&element->HasAttribute("data-event"))element->DispatchEvent("click",{});
        return true;
    }
    void reset_input(){scroll_drags.clear();std::set<Rml::Element*> owned=pulses;for(auto&[id,p]:pointers)owned.insert(p.element);for(auto* e:owned)cancel_element(e);}
    bool focused()const{return context&&interactive(context->GetFocusElement())&&context->GetFocusElement()->IsVisible(true);}
    void focus_next(bool forward){
        if(!context||focusables.empty())return;
        const auto found=std::find(focusables.begin(),focusables.end(),context->GetFocusElement());
        int position=found==focusables.end()?(forward?-1:0):int(found-focusables.begin());
        for(std::size_t i=0;i<focusables.size();++i){position=(position+(forward?1:-1)+int(focusables.size()))%int(focusables.size());auto* next=focusables[position];if(next->IsVisible(true)&&!next->HasAttribute("disabled")){if(next->Focus(true)){prune();return;}}}
    }
};
UiRenderer::UiRenderer(const Project& project,const std::filesystem::path& executable,bool headless){
    // Keep the CPU preflight alive while creating the actual context so both use
    // one font runtime, and old/new contexts coexist safely during a hot reload.
    std::unique_ptr<Impl> checked;
    if(!headless)checked=std::make_unique<Impl>(project,executable,true);
    impl_=std::make_unique<Impl>(project,executable,headless);
}
UiRenderer::~UiRenderer()=default;
bool UiRenderer::captures_keyboard()const{return impl_->focused()&&!impl_->context->GetFocusElement()->HasAttribute("data-action");}
bool UiRenderer::consumes_key(int code)const{return impl_->focused()&&impl_->context->GetFocusElement()->HasAttribute("data-action")&&(code==SDL_SCANCODE_SPACE||code==SDL_SCANCODE_RETURN);}
bool UiRenderer::consumes_gamepad_button(int button)const{return impl_->focused()&&impl_->context->GetFocusElement()->HasAttribute("data-action")&&button==SDL_GAMEPAD_BUTTON_SOUTH;}
bool UiRenderer::process_pointer(std::int64_t id,PointerPhase phase,float x,float y){return impl_->pointer(id,phase,x,y);}
void UiRenderer::apply(Input& input)const {
    const auto& self=*impl_;std::set<Rml::Element*> active=self.pulses;
    for(const auto&[id,p]:self.pointers)if(p.active)active.insert(p.element);
    for(auto* e:active)if(self.available(e)){
        const auto action=e->GetAttribute<Rml::String>("data-action","");if(!self.actions.contains(action))continue;
        if(action=="fire")input.fire=true;else if(action=="jump")input.jump=true;else input.actions[action]=1;
    }
}
void UiRenderer::consume_tick(){impl_->pulses.clear();}
void UiRenderer::reset_input(){impl_->reset_input();}
bool UiRenderer::process_event(const SDL_Event& event) {
    auto& self=*impl_;if(!self.context)return false;auto* context=self.context;const int mods=modifiers();
    switch(event.type) {
    case SDL_EVENT_MOUSE_MOTION:{float ratio=1;if(auto* window=SDL_GetWindowFromID(event.motion.windowID))ratio=SDL_GetWindowPixelDensity(window);self.mouse_x=event.motion.x*ratio;self.mouse_y=event.motion.y*ratio;const bool held=self.pointer(-1,PointerPhase::move,self.mouse_x,self.mouse_y);return !context->ProcessMouseMove(int(self.mouse_x)-self.safe[0],int(self.mouse_y)-self.safe[1],mods)||held;}
    case SDL_EVENT_MOUSE_BUTTON_DOWN:case SDL_EVENT_MOUSE_BUTTON_UP:{const int button=event.button.button==SDL_BUTTON_LEFT?0:event.button.button==SDL_BUTTON_RIGHT?1:2;if(button==0&&self.pointer(-1,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN?PointerPhase::down:PointerPhase::up,self.mouse_x,self.mouse_y))return true;return event.type==SDL_EVENT_MOUSE_BUTTON_DOWN?!context->ProcessMouseButtonDown(button,mods):!context->ProcessMouseButtonUp(button,mods);}
    case SDL_EVENT_MOUSE_WHEEL:return !context->ProcessMouseWheel(Rml::Vector2f{-event.wheel.x,-event.wheel.y},mods);
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:self.pointer(-1,PointerPhase::cancel,0,0);context->ProcessMouseLeave();return false;
    case SDL_EVENT_WINDOW_FOCUS_LOST:case SDL_EVENT_WINDOW_HIDDEN:case SDL_EVENT_WILL_ENTER_BACKGROUND:case SDL_EVENT_GAMEPAD_REMOVED:self.reset_input();return false;
    case SDL_EVENT_TEXT_INPUT:return !context->ProcessTextInput(Rml::String(event.text.text))||self.focused();
    case SDL_EVENT_KEY_DOWN:
        if(event.key.key==SDLK_TAB){self.focus_next(!(mods&Rml::Input::KM_SHIFT));return self.focused();}
        if(self.focused()&&(event.key.key==SDLK_ESCAPE||event.key.key==SDLK_AC_BACK)){auto* focused=context->GetFocusElement();self.reset_input();if(!focused->HasAttribute("data-action")&&self.events.size()<256)self.events.push_back({{"type","ui"},{"id",focused->GetId()},{"event","cancel"},{"value",""}});focused->Blur();return true;}
        if(self.focused()&&context->GetFocusElement()->HasAttribute("data-action")&&(event.key.key==SDLK_RETURN||event.key.key==SDLK_SPACE)){if(!event.key.repeat)self.pointer(event.key.key==SDLK_RETURN?-2:-3,PointerPhase::down,0,0,context->GetFocusElement());return true;}
        if(self.focused() && (context->GetFocusElement()->GetTagName()=="button"||context->GetFocusElement()->HasAttribute("data-event")) && (event.key.key==SDLK_RETURN||event.key.key==SDLK_SPACE)){if(!event.key.repeat)context->GetFocusElement()->DispatchEvent("click",{});return true;}
        return !context->ProcessKeyDown(key(event.key.key),mods)||self.focused();
    case SDL_EVENT_KEY_UP:if((event.key.key==SDLK_RETURN||event.key.key==SDLK_SPACE)&&self.pointer(event.key.key==SDLK_RETURN?-2:-3,PointerPhase::up,0,0))return true;return !context->ProcessKeyUp(key(event.key.key),mods)||self.focused();
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_DOWN||event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_RIGHT){self.focus_next(true);return self.focused();}
        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_UP||event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_LEFT){self.focus_next(false);return self.focused();}
        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH&&self.focused()){if(context->GetFocusElement()->HasAttribute("data-action"))self.pointer(-4,PointerPhase::down,0,0,context->GetFocusElement());else context->GetFocusElement()->DispatchEvent("click",{});return true;}
        return false;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:return event.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH&&self.pointer(-4,PointerPhase::up,0,0);
    default:return false;
    }
}
void UiRenderer::set_safe_area(std::array<int,4> rectangle) {
    if(rectangle!=std::array<int,4>{} && (rectangle[0]<0||rectangle[1]<0||rectangle[2]<1||rectangle[3]<1||rectangle[0]>65535-rectangle[2]||rectangle[1]>65535-rectangle[3]))throw std::runtime_error("Invalid UI safe area");
    if(impl_->requested_safe!=rectangle)impl_->reset_input();impl_->requested_safe=rectangle;
}
void UiRenderer::update(const Json& state,double time,int width,int height,float density) {
    auto& self=*impl_;if(width<1||height<1||width>65535||height>65535)throw std::runtime_error("Invalid UI viewport dimensions");
    if(!std::isfinite(density)||density<=0||density>16)throw std::runtime_error("UI density must be finite and in (0,16]");
    if(self.width!=width||self.height!=height||self.density!=density)self.reset_input();self.density=density;
    self.safe=self.requested_safe==std::array<int,4>{}?std::array<int,4>{0,0,width,height}:self.requested_safe;
    if(self.safe[0]+self.safe[2]>width||self.safe[1]+self.safe[3]>height)throw std::runtime_error("UI safe area exceeds framebuffer");
    self.width=width;self.height=height;if(!self.context)return;
    self.context->SetDensityIndependentPixelRatio(density);self.runtime->system.time=time;self.context->SetDimensions({self.safe[2],self.safe[3]});
    const auto& data=state.contains("ui")?state["ui"]:state;
    if(data.is_object())for(const auto& [id,value]:data.items()) {
        auto found=self.elements.find(id);if(found==self.elements.end())continue;
        if(self.previous.contains(id)&&self.previous[id]==value)continue;
        auto* element=found->second;
        if(value.is_object()) {
            if(value.contains("text"))self.set_text(element,value["text"]);
            if(value.contains("value"))element->SetAttribute("value",text(value["value"]));
            if(value.contains("max"))element->SetAttribute("max",text(value["max"]));
            if(value.contains("visible")&&value["visible"].is_boolean()){if(!value["visible"].get<bool>()){element->SetProperty("display","none");}else element->RemoveProperty("display");}
            if(value.contains("class")&&value["class"].is_string())element->SetClassNames(value["class"].get<std::string>());
            if(value.contains("disabled")&&value["disabled"].is_boolean()){if(value["disabled"].get<bool>())element->SetAttribute("disabled","");else element->RemoveAttribute("disabled");}
            if(value.contains("opacity")){const float opacity=value["opacity"].get<float>();if(!std::isfinite(opacity)||opacity<0||opacity>1)throw std::runtime_error("UI opacity must be within 0..1");element->SetProperty("opacity",std::to_string(opacity));}
        } else if(value.is_primitive()) {
            if(element->GetTagName()=="progress"||element->GetTagName()=="input"||element->GetTagName()=="select")element->SetAttribute("value",text(value));
            else self.set_text(element,value);
        }
        self.previous[id]=value;
    }
    self.context->Update();
    self.prune();
}
void UiRenderer::draw(){auto& self=*impl_;if(!self.context)return;self.renderer->begin(self.width,self.height,self.safe);self.context->Render();}
Json UiRenderer::drain_events(){auto result=std::move(impl_->events);impl_->events=Json::array();return result;}
Json UiRenderer::inspect()const {
    const auto& self=*impl_;Json elements=Json::object();
    Json actions=Json::object();
    for(const auto& [id,element]:self.elements){const auto r=self.bounds(element),hit=self.hit_bounds(element);const auto action=element->GetAttribute<Rml::String>("data-action","");elements[id]={{"tag",element->GetTagName()},{"position",{r[0],r[1]}},{"size",{r[2],r[3]}},{"logical_position",{(r[0]-self.safe[0])/self.density,(r[1]-self.safe[1])/self.density}},{"logical_size",{r[2]/self.density,r[3]/self.density}},{"hit_rect",hit},{"visible",element->IsVisible(true)},{"interactive",interactive(element)},{"enabled",self.available(element)},{"action",action},{"held",self.held(element)},{"value",self.previous.value(id,Json())}};if(!action.empty())actions[action]=actions.value(action,false)||self.held(element);}
    for(const auto& [id,element]:self.elements) {
        auto& record=elements[id];auto* doc=element->GetOwnerDocument();
        std::string source=doc?doc->GetSourceURL():"";
        if(source.starts_with(self.name+"/"))source.erase(0,self.name.size()+1);
        record["document"]=source;
        bool transformed=false;for(auto* p=element;p;p=p->GetParentNode())if(const auto* v=p->GetProperty("transform"))if(v->ToString()!="none"&&!v->ToString().empty())transformed=true;record["transformed"]=transformed;
        auto* parent=element->GetParentNode();record["parent"]=parent?parent->GetId():"";
        auto* offset=element->GetOffsetParent();record["offset_parent_rect"]=offset?Json(self.bounds(offset)):Json(self.safe);
        Json properties=Json::object();
        for(const char* key:{"position","left","top","right","bottom","width","height","margin-left","margin-top","margin-right","margin-bottom","padding-left","padding-top","padding-right","padding-bottom","font-size","color","background-color","opacity","z-index","text-align","display","border-radius"}) {
            if(const auto* property=element->GetProperty(key))properties[key]=property->ToString();
        }
        const auto content_size=element->GetBox().GetSize(Rml::BoxArea::Content);record["content_size"]={content_size.x,content_size.y};
        record["properties"]=std::move(properties);
    }
    return {{"editor_revision",1},{"enabled",self.enabled},{"viewport",{self.width,self.height}},{"width",self.width},{"height",self.height},{"safe_area",self.safe},{"content_rect",self.safe},{"logical_content_size",{self.safe[2]/self.density,self.safe[3]/self.density}},{"insets",{self.safe[0],self.safe[1],self.width-self.safe[0]-self.safe[2],self.height-self.safe[1]-self.safe[3]}},{"density",self.density},{"held_actions",actions},{"owned_pointers",self.pointers.size()},{"focus",self.context&&self.context->GetFocusElement()?self.context->GetFocusElement()->GetId():""},{"elements",std::move(elements)}};
}
Json UiRenderer::diagnostics(const std::string& expected)const {
    const auto& self=*impl_;Json results=Json::array();
    auto issue=[&](const char* code,const std::string& id,const std::string& message,const char* severity="warning"){if(results.size()<256)results.push_back({{"severity",severity},{"code",code},{"id",id},{"message",message}});};
    std::vector<Rml::Element*> controls;
    for(auto* e:self.focusables){
        const auto action=e->GetAttribute<Rml::String>("data-action","");
        if(e->HasAttribute("data-action")&&!self.actions.contains(action))issue("ui_action",e->GetId(),"Register data-action '"+action+"' in input.actions (or use legacy fire/jump).","error");
        if(!self.available(e))continue;
        const auto r=self.bounds(e,true);
        if(r[0]<self.safe[0]||r[1]<self.safe[1]||r[0]+r[2]>self.safe[0]+self.safe[2]||r[1]+r[3]>self.safe[1]+self.safe[3])issue("ui_offscreen",e->GetId(),"Interactive hit region extends outside the safe area.");
        if(r[2]<=0||r[3]<=0)issue("ui_empty_hit_region",e->GetId(),"Interactive element has no positive hit area.");
        if(controls.size()<512)controls.push_back(e);
    }
    if(self.focusables.size()>512)issue("ui_diagnostic_limit","","Overlap analysis is limited to the first 512 active controls.");
    auto ancestor=[](Rml::Element* a,Rml::Element* b){for(auto*p=b->GetParentNode();p;p=p->GetParentNode())if(p==a)return true;return false;};
    for(std::size_t i=0;i<controls.size()&&results.size()<256;++i)for(std::size_t j=i+1;j<controls.size()&&results.size()<256;++j){auto* a=controls[i];auto* b=controls[j];if(ancestor(a,b)||ancestor(b,a)||a->HasAttribute("data-allow-overlap")||b->HasAttribute("data-allow-overlap"))continue;const auto x=self.hit_bounds(a),y=self.hit_bounds(b);if(x[2]>0&&x[3]>0&&y[2]>0&&y[3]>0&&x[0]<y[0]+y[2]&&y[0]<x[0]+x[2]&&x[1]<y[1]+y[3]&&y[1]<x[1]+x[3])issue("ui_overlap",a->GetId(),"Interactive hit region overlaps '"+b->GetId()+"'. Review routing, or mark intentional overlap with data-allow-overlap.");}
    if(!self.enabled)return results;
    struct FontCheck {FT_Library library=nullptr;std::map<std::string,FT_Face> faces;~FontCheck(){for(auto&[id,face]:faces)FT_Done_Face(face);if(library)FT_Done_FreeType(library);}} fonts;
    if(FT_Init_FreeType(&fonts.library)){issue("ui_font_check","","Could not initialize font coverage checking.","error");return results;}
    auto lower=[](std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;};
    for(const auto&[family,bytes]:self.font_sources){FT_Face face=nullptr;if(!FT_New_Memory_Face(fonts.library,reinterpret_cast<const FT_Byte*>(bytes.data()),static_cast<FT_Long>(bytes.size()),0,&face))fonts.faces[lower(family)]=face;}
    std::set<std::pair<std::string,std::uint32_t>> reported;std::size_t checked_bytes=0;
    auto check_text=[&](const std::string& text,const std::string& family,const std::string& id){
        const auto requested=fonts.faces.find(lower(family)),fallback=fonts.faces.find("langmake");
        for(std::size_t i=0;i<text.size()&&results.size()<256&&checked_bytes<1024*1024;){
            std::uint32_t cp=static_cast<unsigned char>(text[i++]);++checked_bytes;
            int remaining=cp<0x80?0:cp<0xe0?1:cp<0xf0?2:3;
            if(remaining){cp&=(1u<<(6-remaining))-1;for(int n=0;n<remaining&&i<text.size();++n){cp=(cp<<6)|(static_cast<unsigned char>(text[i++])&63);++checked_bytes;}}
            if(cp<32||cp==127)continue;
            if((requested!=fonts.faces.end()&&FT_Get_Char_Index(requested->second,cp))||(fallback!=fonts.faces.end()&&FT_Get_Char_Index(fallback->second,cp)))continue;
            if(!reported.emplace(family,cp).second)continue;
            std::ostringstream name;name<<"U+"<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<cp;
            issue("ui_missing_glyph",id,"Missing "+name.str()+" in font '"+family+"' and the bundled fallback. Register a font with this glyph.");
        }
    };
    std::set<std::string> families;
    std::function<void(Rml::Element*,std::string)> visit=[&](Rml::Element* e,std::string id){if(!e->GetId().empty())id=e->GetId();if(auto* text=dynamic_cast<Rml::ElementText*>(e)){const auto family=e->GetComputedValues().font_family();families.insert(family);check_text(text->GetText(),family,id);}for(int i=0;i<e->GetNumChildren();++i)visit(e->GetChild(i),id);};
    for(auto* document:self.documents)visit(document,"");
    if(families.empty())families.insert("Langmake");
    for(const auto& family:families){check_text(self.expected_characters,family,"expected_characters");check_text(expected,family,"expected_characters");}
    if(checked_bytes>=1024*1024)issue("ui_diagnostic_limit","","Glyph analysis is limited to 1 MiB of text.");
    return results;
}
}
