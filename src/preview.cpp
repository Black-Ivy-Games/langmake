#include "langmake/preview.hpp"
#include "langmake/mobile_platform.hpp"
#include "langmake/project.hpp"
#include "langmake/world.hpp"
#include "langmake/audio.hpp"
#include "langmake/asset_renderer.hpp"
#include "langmake/io.hpp"
#include "langmake/timeline.hpp"
#include "langmake/ui.hpp"
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <lodepng/lodepng.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <coroutine>
#include <exception>
#include <functional>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(LANGMAKE_MOBILE)
#include "langmake/android_input.hpp"
#include "langmake/android_recovery.hpp"
#ifdef __ANDROID__
#include <android/native_window.h>
#else
#include <SDL3/SDL_metal.h>
#endif
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#if defined(__APPLE__) && !defined(LANGMAKE_IOS)
#include <SDL3/SDL_metal.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace lm {
namespace {
constexpr float pi = 3.14159265358979323846f;
#ifdef _WIN32
struct PreviewLock {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit PreviewLock(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot acquire preview lock; another preview may already be running for this project.");
    }
    ~PreviewLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
#elif defined(LANGMAKE_MOBILE) || defined(__APPLE__)
struct PreviewLock {
    int descriptor=-1;
    explicit PreviewLock(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        descriptor=open(path.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
        if(descriptor<0||flock(descriptor,LOCK_EX|LOCK_NB)!=0){if(descriptor>=0)close(descriptor);throw std::runtime_error("Another mobile player owns this storage session");}
    }
    ~PreviewLock(){if(descriptor>=0)close(descriptor);}
};
#endif
#ifdef LANGMAKE_MOBILE
struct MobileLifecycle {
    std::atomic<bool> background{false},terminating{false},memory_warning{false};
    std::atomic<std::uint64_t> background_epoch{0};
    bool installed=false;
    static bool SDLCALL event(void* context,SDL_Event* event) {
        auto& state=*static_cast<MobileLifecycle*>(context);
        if(event->type==SDL_EVENT_WILL_ENTER_BACKGROUND){state.background=true;++state.background_epoch;}
        if(event->type==SDL_EVENT_DID_ENTER_BACKGROUND)state.background=true;
        if(event->type==SDL_EVENT_DID_ENTER_FOREGROUND)state.background=false;
        if(event->type==SDL_EVENT_TERMINATING)state.terminating=true;
        if(event->type==SDL_EVENT_LOW_MEMORY)state.memory_warning=true;
        return true;
    }
    MobileLifecycle(){
#ifndef LANGMAKE_IOS
        installed=SDL_AddEventWatch(event,this);if(!installed)throw std::runtime_error(SDL_GetError());
#endif
    }
    void stop(){if(installed){SDL_RemoveEventWatch(event,this);installed=false;}}
    ~MobileLifecycle(){stop();}
};
#ifdef __ANDROID__
struct AndroidSurface {
    ANativeWindow* window=nullptr;
    ~AndroidSurface(){reset();}
    void reset(){if(window){ANativeWindow_release(window);window=nullptr;}}
    void acquire(SDL_Window* sdl) {
        reset();const auto properties=SDL_GetWindowProperties(sdl);
        if(!SDL_LockProperties(properties))throw std::runtime_error(SDL_GetError());
        window=static_cast<ANativeWindow*>(SDL_GetPointerProperty(properties,SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,nullptr));
        if(window)ANativeWindow_acquire(window);
        SDL_UnlockProperties(properties);
    }
    bool matches(SDL_Window* sdl)const{return window&&window==SDL_GetPointerProperty(SDL_GetWindowProperties(sdl),SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,nullptr);}
};
#else
struct IosSurface {
    SDL_MetalView view=nullptr;
    ~IosSurface(){reset();}
    void reset(){if(view){SDL_Metal_DestroyView(view);view=nullptr;}}
    void acquire(SDL_Window* window){reset();view=SDL_Metal_CreateView(window);if(!view)throw std::runtime_error(SDL_GetError());}
};
#endif
#endif
struct Vertex { float x, y, z; uint32_t color; };
uint32_t rgba(const Color& c, float shade = 1) {
    auto byte = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return byte(c[0] * shade) | (byte(c[1] * shade) << 8) | (byte(c[2] * shade) << 16) | (byte(c[3]) << 24);
}

class RenderCallbacks final : public bgfx::CallbackI {
public:
    std::mutex mutex;
    std::map<std::string, std::string> captures;
    void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* message) override {
        std::fprintf(stderr, "bgfx fatal: %s\n", message); std::abort();
    }
    void traceVargs(const char*, uint16_t, const char* format, va_list args) override {
        if (std::getenv("LANGMAKE_RENDER_TRACE")) std::vfprintf(stderr, format, args);
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
        bgfx::TextureFormat::Enum format, const void* data, uint32_t, bool flip) override {
        std::string error;
        if (std::getenv("LANGMAKE_RENDER_TRACE")) std::fprintf(stderr, "capture callback: %s %ux%u format=%d\n", path, width, height, int(format));
        try {
            std::vector<unsigned char> pixels(size_t(width) * height * 4);
            auto* bytes = static_cast<const unsigned char*>(data);
            for (uint32_t y = 0; y < height; ++y) {
                const auto* row = bytes + (flip ? height - y - 1 : y) * pitch;
                for (uint32_t x = 0; x < width; ++x) {
                    auto* pixel = &pixels[(size_t(y) * width + x) * 4];
                    pixel[0] = row[x * 4 + (format == bgfx::TextureFormat::BGRA8 ? 2 : 0)];
                    pixel[1] = row[x * 4 + 1];
                    pixel[2] = row[x * 4 + (format == bgfx::TextureFormat::BGRA8 ? 0 : 2)];
                    pixel[3] = 255;
                }
            }
            std::vector<unsigned char> png;
            if (unsigned status = lodepng::encode(png, pixels, width, height)) error = lodepng_error_text(status);
            else {
                std::ofstream out(std::filesystem::path(path), std::ios::binary);
                out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
                if (!out) error = "Could not write capture";
            }
        } catch (const std::exception& e) { error = e.what(); }
        std::lock_guard guard(mutex); captures[path] = error;
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

bgfx::ShaderHandle shader(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Missing renderer shader: " + path.string());
    const auto size = in.tellg();
    if (size <= 0 || size > 1024 * 1024) throw std::runtime_error("Invalid renderer shader size");
    auto memory = bgfx::alloc(uint32_t(size));
    in.seekg(0); in.read(reinterpret_cast<char*>(memory->data), size);
    return bgfx::createShader(memory);
}

void box_mesh(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices, const Entity& entity, bool flat) {
    constexpr float corners[8][3]{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    constexpr int faces[6][4]{{4,5,6,7},{1,0,3,2},{0,4,7,3},{5,1,2,6},{3,7,6,2},{0,1,5,4}};
    constexpr float shades[6]{0.85f,0.58f,0.66f,0.76f,1.0f,0.48f};
    for (int face = 0; face < 6; ++face) {
        auto start = uint16_t(vertices.size());
        for (int c : faces[face]) {
            vertices.push_back({entity.position[0] + corners[c][0] * entity.size[0] * .5f,
                entity.position[1] + corners[c][1] * entity.size[1] * .5f,
                entity.position[2] + corners[c][2] * entity.size[2] * .5f,
                rgba(entity.color, flat ? 1.0f : shades[face])});
        }
        for (int offset : {0,1,2,0,2,3}) indices.push_back(uint16_t(start + offset));
    }
}
void sphere_mesh(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices, const Entity& e, bool flat) {
    constexpr int rings = 10, slices = 16;
    const auto start = uint16_t(vertices.size());
    for (int r = 0; r <= rings; ++r) for (int s = 0; s <= slices; ++s) {
        float latitude = pi * r / rings, longitude = 2 * pi * s / slices;
        float nx = std::sin(latitude) * std::cos(longitude), ny = std::cos(latitude), nz = std::sin(latitude) * std::sin(longitude);
        float light = flat ? 1 : std::clamp(.62f + ny * .24f + nz * .12f - nx * .1f, .35f, 1.0f);
        const float y=e.shape=="capsule"?ny*e.size[0]*.5f+(ny>=0?1:-1)*std::max(0.f,e.size[1]-e.size[0])*.5f:ny*e.size[1]*.5f;
        vertices.push_back({e.position[0] + nx * e.size[0] * .5f, e.position[1] + y, e.position[2] + nz * e.size[2] * .5f, rgba(e.color, light)});
    }
    for (int r = 0; r < rings; ++r) for (int s = 0; s < slices; ++s) {
        uint16_t a = start + r * (slices + 1) + s, b = a + slices + 1;
        for (auto i : {a,b,uint16_t(a+1),uint16_t(a+1),b,uint16_t(b+1)}) indices.push_back(i);
    }
}
void submit_mesh(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices,
    const bgfx::VertexLayout& layout, bgfx::ProgramHandle program, uint16_t view, bool depth) {
    if (vertices.empty() || indices.empty()) return;
    bgfx::TransientVertexBuffer vb; bgfx::TransientIndexBuffer ib;
    if (!bgfx::allocTransientBuffers(&vb, layout, uint32_t(vertices.size()), &ib, uint32_t(indices.size())))
        throw std::runtime_error("Scene exceeds available transient geometry budget");
    std::memcpy(vb.data, vertices.data(), vertices.size() * sizeof(Vertex));
    std::memcpy(ib.data, indices.data(), indices.size() * sizeof(uint16_t));
    bgfx::setVertexBuffer(0, &vb); bgfx::setIndexBuffer(&ib);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA | BGFX_STATE_BLEND_ALPHA |
        (depth ? BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS : uint64_t(0)));
    bgfx::submit(view, program);
}
void debug_lines(const std::vector<Entity>& entities,const Json& state,const std::vector<std::string>& enabled,
                 const bgfx::VertexLayout& layout,bgfx::ProgramHandle program) {
    auto has=[&](const char* name){return std::find(enabled.begin(),enabled.end(),name)!=enabled.end();};
    std::vector<Vertex> lines;
    auto line=[&](Vec3 a,Vec3 b,Color color){if(lines.size()>60000)return;const auto c=rgba(color);lines.push_back({a[0],a[1],a[2],c});lines.push_back({b[0],b[1],b[2],c});};
    auto position=[&](const std::string&id){for(const auto&e:entities)if(e.id==id)return e.position;return Vec3{0,0,0};};
    if(has("collision"))for(const auto&e:entities)if(e.collision_enabled&&e.body!="none") {
        if(e.physics2d.contains("polygons")) {
            const float a=e.rotation[2]*3.14159265f/180,co=std::cos(a),si=std::sin(a);
            auto point=[&](const Json& v){const float x=v[0].get<float>()*e.size[0],y=v[1].get<float>()*e.size[1];return Vec3{e.position[0]+co*x-si*y,e.position[1]+si*x+co*y,e.position[2]+.1f};};
            for(const auto& poly:e.physics2d["polygons"])for(size_t i=0;i<poly.size();++i)line(point(poly[i]),point(poly[(i+1)%poly.size()]),{0,1,1,1});
            continue;
        }
        Vec3 corners[8];for(int i=0;i<8;++i)for(int a=0;a<3;++a)corners[i][a]=e.position[a]+e.size[a]*((i&(1<<a))?.5f:-.5f);
        for(int i=0;i<8;++i)for(int a=0;a<3;++a)if(!(i&(1<<a)))line(corners[i],corners[i|(1<<a)],{0,1,1,1});
    }
    const auto navigation=state.value("navigation_agents",Json::object());
    if(has("navigation"))for(const auto&[id,agent]:navigation.items()) {
        auto from=position(id);if(agent.contains("goal"))line(from,agent["goal"].get<Vec3>(),{.3f,1,.2f,1});
        if(agent.contains("points")&&agent["points"].is_array())for(const auto&point:agent["points"]){auto next=point.get<Vec3>();line(from,next,{1,1,0,1});from=next;}
    }
    auto camera=state.value("camera",Json::object());
    if(has("camera")&&camera.contains("position")&&camera.contains("target"))line(camera["position"].get<Vec3>(),camera["target"].get<Vec3>(),{1,.25f,1,1});
    if(has("targeting")) {
        auto target=camera.value("target_entity",std::string());auto follower=camera.value("follow",std::string());
        if(!target.empty()&&!follower.empty())line(position(follower),position(target),{1,.3f,.1f,1});
        for(const auto&e:entities)if(e.role=="target"||e.role=="enemy") {auto a=e.position,b=a;a[0]-=.6f;b[0]+=.6f;line(a,b,{1,.3f,.1f,1});a=e.position;b=a;a[1]-=.6f;b[1]+=.6f;line(a,b,{1,.3f,.1f,1});}
    }
    if(lines.empty())return;
    bgfx::TransientVertexBuffer vb;if(bgfx::getAvailTransientVertexBuffer(uint32_t(lines.size()),layout)<lines.size())return;
    bgfx::allocTransientVertexBuffer(&vb,uint32_t(lines.size()),layout);
    std::memcpy(vb.data,lines.data(),lines.size()*sizeof(Vertex));bgfx::setVertexBuffer(0,&vb);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_PT_LINES);bgfx::submit(0,program);
}
Json read_json(const std::filesystem::path& path) { return read_json_shared(path); }
std::string single_line(std::string text, size_t maximum) {
    for (char& c : text) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = ' ';
    if (text.size() > maximum) text = text.substr(0, maximum > 3 ? maximum - 3 : 0) + "...";
    return text;
}
struct PendingCapture {
    std::filesystem::path path;
    Json metadata;
    std::filesystem::path response;
    std::string request_id;
};
struct PreviewEvents {
    struct Queued { SDL_Event event;std::string text; };
    bool callbacks=false,stop=false;
    std::deque<Queued> queue;
    std::string current_text;
    std::function<void(const SDL_Event&)> lifecycle;
    void push(const SDL_Event& event) {
        if(lifecycle)lifecycle(event);
        // The player does not consume drop, user or IME candidate payloads.
        // Never retain borrowed pointers from those event structures.
        if(event.type==SDL_EVENT_TEXT_EDITING||event.type==SDL_EVENT_TEXT_EDITING_CANDIDATES||
           (event.type>=SDL_EVENT_DROP_FILE&&event.type<=SDL_EVENT_DROP_POSITION)||event.type>=SDL_EVENT_USER)return;
        if(queue.size()>=4096)throw std::runtime_error("Platform input queue capacity exceeded");
        queue.push_back({event,event.type==SDL_EVENT_TEXT_INPUT&&event.text.text?event.text.text:""});
    }
    bool next(SDL_Event& event) {
        if(!callbacks)return SDL_PollEvent(&event);
        if(queue.empty())return false;
        event=queue.front().event;current_text=std::move(queue.front().text);queue.pop_front();
        if(event.type==SDL_EVENT_TEXT_INPUT)event.text.text=current_text.c_str();
        return true;
    }
};
struct PreviewTask {
    struct promise_type {
        Json result;
        std::exception_ptr error;
        PreviewTask get_return_object(){return PreviewTask{std::coroutine_handle<promise_type>::from_promise(*this)};}
        std::suspend_always initial_suspend()noexcept{return {};}
        std::suspend_always final_suspend()noexcept{return {};}
        void return_value(Json value){result=std::move(value);}
        void unhandled_exception()noexcept{error=std::current_exception();}
    };
    std::coroutine_handle<promise_type> handle;
    explicit PreviewTask(std::coroutine_handle<promise_type> value):handle(value){}
    PreviewTask(PreviewTask&& other)noexcept:handle(std::exchange(other.handle,{})){}
    ~PreviewTask(){if(handle)handle.destroy();}
};
}

static PreviewTask preview_frames(Project project, PreviewOptions options, PreviewEvents* events) {
#if !defined(_WIN32) && !defined(LANGMAKE_MOBILE) && !defined(__APPLE__)
    throw std::runtime_error("Graphical preview is available on Windows, Android and iOS player builds; headless operations remain available.");
    co_return Json{};
#else
    const auto session = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const auto storage=options.storage_directory.empty()?project.root/".langmake":options.storage_directory;
    const auto bridge = options.isolated ? storage / "sessions" / session : storage;
    PreviewLock preview_lock(bridge / "preview.lock");
    Publications publications;
    Timeline timeline(options.timeline);
    int scenario_tick=0;
    size_t next_capture_tick=0;
    std::uint64_t publication_sequence=0;
    std::map<std::string,std::int64_t> handled_requests;
    auto world = std::make_unique<World>(project);
    std::string recovery_status;
#ifdef LANGMAKE_MOBILE
    Json rejected_recovery;bool recovery_writable=true;
#ifdef LANGMAKE_IOS
    constexpr const char* recovery_slot="_ios_resume";
#else
    constexpr const char* recovery_slot="_android_resume";
#endif
    const auto recovery_path=storage/"saves"/(std::string(recovery_slot)+".json");
    if(options.restore_recovery&&std::filesystem::exists(recovery_path)) {
        try{world->restore(read_json_shared(recovery_path,16*1024*1024));project=world->current_project();recovery_status="restored";}
        catch(const std::exception& error){
            recovery_status=std::string("Recovery was not compatible: ")+error.what();
            rejected_recovery=quarantine_mobile_recovery(recovery_path,recovery_status);
            recovery_writable=rejected_recovery.value("preserved",false);
            if(!recovery_writable)recovery_status+="; automatic saves blocked because rejected progress could not be preserved";
        }
    }
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"0");
#endif
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
#endif
    const auto live_path = bridge / "live.json";
    std::filesystem::create_directories(bridge / "commands");
    std::filesystem::create_directories(bridge / "results");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
#ifdef LANGMAKE_MOBILE
    MobileLifecycle lifecycle;
#ifdef __ANDROID__
    AndroidSurface native_surface;
#else
    IosSurface native_surface;
#endif
    const auto properties=SDL_CreateProperties();
    SDL_SetStringProperty(properties,SDL_PROP_WINDOW_CREATE_TITLE_STRING,("Langmake | "+project.title).c_str());
    SDL_SetNumberProperty(properties,SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,options.width);SDL_SetNumberProperty(properties,SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,options.height);
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,true);
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,true);
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN,true);
#ifdef __ANDROID__
    // bgfx owns EGL; SDL must not create a competing EGL context or surface.
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN,true);
#else
    SDL_SetBooleanProperty(properties,SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN,true);
#endif
    auto* window=SDL_CreateWindowWithProperties(properties);SDL_DestroyProperties(properties);
    std::unique_ptr<MobileControls> mobile;
#else
    auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __APPLE__
    flags |= SDL_WINDOW_METAL;
    SDL_MetalView metal_view = nullptr;
#endif
    auto* window = SDL_CreateWindow(("Langmake | " + project.title).c_str(), options.width, options.height, flags);
#endif
    if (!window) {
        const std::string error=SDL_GetError();
#ifdef LANGMAKE_MOBILE
        lifecycle.stop();
#endif
        SDL_Quit();throw std::runtime_error(error);
    }
    RenderCallbacks callbacks;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    std::unique_ptr<AssetRenderer> assets;
    std::unique_ptr<UiRenderer> ui;
    bool renderer_ready = false;
    SDL_Gamepad* controller = nullptr;
    Json final_state;
    try {
        int width = options.width, height = options.height;
        auto initialize_graphics=[&] {
        bgfx::Init init;
#ifdef LANGMAKE_MOBILE
        native_surface.acquire(window);
#ifdef __ANDROID__
        if(!native_surface.window)throw std::runtime_error("Android has not provided a render surface");
        init.type=bgfx::RendererType::OpenGLES;init.swapChain.nwh=native_surface.window;
#else
        init.type=bgfx::RendererType::Metal;init.swapChain.nwh=SDL_Metal_GetLayer(native_surface.view);
        if(!init.swapChain.nwh)throw std::runtime_error("SDL did not provide a Metal layer");
        // UIKit/layer ownership stays on this thread, including shutdown.
        bgfx::renderFrame();
#endif
#elif defined(__APPLE__)
        metal_view=SDL_Metal_CreateView(window);
        if(!metal_view)throw std::runtime_error(SDL_GetError());
        init.type=bgfx::RendererType::Metal;init.swapChain.nwh=SDL_Metal_GetLayer(metal_view);
        if(!init.swapChain.nwh)throw std::runtime_error("SDL did not provide a Metal layer");
        bgfx::renderFrame();
#else
        init.type = bgfx::RendererType::Direct3D11;
        init.swapChain.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (!init.swapChain.nwh) throw std::runtime_error("SDL did not provide a native Windows window handle");
#endif
        SDL_GetWindowSizeInPixels(window, &width, &height);
        init.swapChain.width = width; init.swapChain.height = height;
#ifndef LANGMAKE_MOBILE
        init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
#endif
        init.reset = BGFX_RESET_VSYNC;
        init.fallback = false;
        init.callback = &callbacks;
        if (!bgfx::init(init)) throw std::runtime_error("Could not initialize bgfx platform renderer");
        renderer_ready = true;
        if (bgfx::getRendererType() != init.type)
            throw std::runtime_error(std::string("Unexpected renderer: ") + bgfx::getRendererName(bgfx::getRendererType()));
        program = bgfx::createProgram(shader(options.executable_directory / "shaders/vs_cubes.bin"), shader(options.executable_directory / "shaders/fs_cubes.bin"), true);
        if (!bgfx::isValid(program)) throw std::runtime_error("Could not create preview shader program");
        assets = std::make_unique<AssetRenderer>(world->assets());
        ui=std::make_unique<UiRenderer>(project,options.executable_directory);
#ifdef LANGMAKE_MOBILE
        mobile=std::make_unique<MobileControls>(project.document.value("input",Json::object()),options.executable_directory);
#endif
        bgfx::setDebug(BGFX_DEBUG_TEXT);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x101a2cff, 1.0f, 0);
        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
        };
        initialize_graphics();
        for(auto&[id,scene]:project.scenes){UiRenderer checked(*scene,options.executable_directory,true);}
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
        auto audio=std::make_unique<Audio>(options.audio,project.document.value("audio",Json::object()));
        uint64_t audio_serial = 0;
        auto start_music = [&](bool reset_cursor = false) {
            audio->stop_all();
            // Scene changes and saves preserve a monotonic event serial. Only
            // a newly authored World should replay its start-hook events.
            if(reset_cursor)audio_serial=0;
            const auto state=world->snapshot();const auto music=state.value("music",project.music);
            if(!music.empty())audio->set_music(world->assets().music.at(music),state.value("music_loop",true),state.value("music_bus",std::string("music")));
            if(state.contains("music_layers"))for(auto&[id,layer]:state["music_layers"].items())audio->set_music_layer(id,layer.at("gain").get<float>(),0,"immediate");
            if(state.contains("audio_buses"))for(auto&[id,bus]:state["audio_buses"].items())audio->set_bus(id,bus.value("gain",1.f),bus.value("pan",0.f),bus.value("mute",false));
        };
        auto update_audio = [&] {
            for (const auto& event : world->audio_events()) {
                auto serial = event.at("serial").get<uint64_t>();
                if (serial <= audio_serial) continue;
                audio_serial=serial;
                if(event.value("scene",project.scene_id)!=project.scene_id)continue;
                auto action = event.at("action").get<std::string>();
                if(action=="layer")audio->set_music_layer(event.at("layer").get<std::string>(),event.value("gain",1.f),event.value("fade_seconds",.1f),event.value("quantization",std::string("immediate")));
                if(action=="bus")audio->set_bus(event.at("bus").get<std::string>(),event.value("gain",1.f),event.value("pan",0.f),event.value("mute",false));
                else if (action == "stop_music") audio->stop_music();
                else if(action=="stop_all")audio->stop_all();
                else if(action=="sound" || action=="music") {
                    auto score = world->assets().music.at(event.at("asset").get<std::string>());
                    if (action == "sound") audio->play_sfx(score,event.value("bus",std::string("sfx")));
                    else audio->set_music(score, event.value("loop", true),event.value("bus",std::string("music")));
                }
            }
        };
        start_music(); update_audio();
        int controller_count = 0;
        auto* controllers = SDL_GetGamepads(&controller_count);
        if (controller_count > 0) controller = SDL_OpenGamepad(controllers[0]);
        SDL_free(controllers);
        bool running = true, paused = options.paused, overlay = options.overlay, follow = false;
        bool jump_pending = false, capture_key = false;
        int frame = 0, previous_collected = 0;
        bool previous_won = false;
        float zoom = 1.0f, yaw = .65f, pitch = .62f, camera_distance = 37.0f;
        bool developer_camera=false;
        Vec3 camera_target = project.camera_target;
        auto reset_camera = [&] {
            camera_target = project.camera_target;
            const auto& p = project.camera_position;
            float dx = p[0] - camera_target[0], dy = p[1] - camera_target[1], dz = p[2] - camera_target[2];
            camera_distance = std::max(1.0f, std::sqrt(dx*dx + dy*dy + dz*dz));
            yaw = std::atan2(dx, dz);
            pitch = std::asin(std::clamp(dy / camera_distance, -.999f, .999f));
            zoom = 1.0f;
            developer_camera=false;
        };
        reset_camera();
        std::string selected;
        std::string error, reload_status = options.watch_source?"READY  /  SOURCE WATCHING":"READY  /  PACKAGED CONTENT";
        std::string runtime_error_text;
        std::string rejected_revision;
        std::vector<PendingCapture> pending;
        std::vector<std::string> completed;
        int capture_frame = -1;
        bool requested_final_capture = false;
        auto last_time = std::chrono::steady_clock::now();
        auto last_poll = last_time;
        auto last_live = last_time;
        double accumulator = 0;
        Json pending_input_events=Json::array();
        float frame_ms = 16.67f;
        std::deque<float> frame_samples;
        float view[16], projection[16], view_projection[16];
        auto snapshot = [&] {
            Json state = world->snapshot();
            state["ok"] = true; state["running"] = running; state["paused"] = paused;
            state["session"] = session; state["source_revision"] = project.revision;
            state["selected"] = selected; state["reload_status"] = reload_status;
            state["diagnostic"] = error;
#ifdef LANGMAKE_MOBILE
            #ifdef LANGMAKE_IOS
            state["renderer"]="Metal";state["platform"]="ios";
#else
            state["renderer"]="OpenGL ES";state["platform"]="android";
#endif
            state["background"]=lifecycle.background.load();state["surface_ready"]=renderer_ready;
            state["recovery"]=recovery_status;state["low_memory_observed"]=lifecycle.memory_warning.load();
            if(!rejected_recovery.is_null())state["rejected_recovery"]=rejected_recovery;
            state["storage_directory"]=storage.string();if(mobile)state["touch"]=mobile->inspect();
#elif defined(__APPLE__)
            state["renderer"]="Metal";state["platform"]="macos";
#else
            state["renderer"] = "Direct3D 11";
#endif
            if(ui)state["ui_renderer"]=ui->inspect();
            state["audio_available"] = audio->available(); state["frame_ms"] = frame_ms;
            state["audio_dropped_commands"] = audio->dropped_commands();
            state["audio_mixer"]=audio->measurements();
            state["api_version"]=2;state["mode"]=options.player?"play":"preview";
            state["publication"]=publications.status();state["published_at_ms"]=wall_time_ms();
            state["publication_sequence"]=publication_sequence;
            state["profile"]={{"runtime",world->statistics()}};
            const auto* stats=renderer_ready?bgfx::getStats():nullptr;
            if(stats)
            state["profile"]={{"runtime",world->statistics()},{"render_cpu_ms",stats->cpuTimerFreq>0?1000.0*(stats->cpuTimeEnd-stats->cpuTimeBegin)/stats->cpuTimerFreq:0.0},
                {"gpu_ms",stats->gpuTimerFreq>0?1000.0*(stats->gpuTimeEnd-stats->gpuTimeBegin)/stats->gpuTimerFreq:0.0},{"gpu_frame",stats->gpuFrameNum},{"draw_calls",stats->numDraw},{"textures",stats->numTextures},
                {"transient_vertex_bytes",stats->transientVbUsed},{"transient_index_bytes",stats->transientIbUsed}};
#ifdef _WIN32
            PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
            if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))){state["profile"]["working_set_bytes"]=memory.WorkingSetSize;state["profile"]["private_bytes"]=memory.PrivateUsage;}
#endif
            state["debug"]=options.debug;
            auto samples=std::vector<float>(frame_samples.begin(),frame_samples.end());std::sort(samples.begin(),samples.end());
            state["profile"]["frame_samples"]=samples.size();
            state["profile"]["frame_p50_ms"]=samples.empty()?0:samples[samples.size()/2];
            state["profile"]["frame_p95_ms"]=samples.empty()?0:samples[std::min(samples.size()-1,samples.size()*95/100)];
            state["profile"]["frame_max_ms"]=samples.empty()?0:samples.back();
            return state;
        };
        std::unique_ptr<AssetRenderer> prepared_assets;
        std::unique_ptr<UiRenderer> prepared_ui;
        std::unique_ptr<Audio> prepared_audio;
        auto destroy_graphics=[&] {
#ifdef LANGMAKE_MOBILE
            if(mobile)mobile->reset(ui.get());mobile.reset();
#endif
            prepared_ui.reset();prepared_assets.reset();prepared_audio.reset();ui.reset();assets.reset();
            if(renderer_ready){if(bgfx::isValid(program))bgfx::destroy(program);program=BGFX_INVALID_HANDLE;bgfx::shutdown();renderer_ready=false;}
#ifdef LANGMAKE_MOBILE
            native_surface.reset();
#endif
        };
        auto prepare_scene=[&](const Project& candidate,const CompiledAssets& compiled){
            auto next_assets=std::make_unique<AssetRenderer>(compiled);
            auto next_ui=std::make_unique<UiRenderer>(candidate,options.executable_directory);
            auto next_audio=std::make_unique<Audio>(options.audio,candidate.document.value("audio",Json::object()));
            prepared_assets=std::move(next_assets);prepared_ui=std::move(next_ui);prepared_audio=std::move(next_audio);
        };
        world->set_scene_prepare(prepare_scene);
        auto reload = [&] {
            try {
                auto next_project = options.watch_source ? load_project(project.file,project.profile) : project;
                auto next_world = std::make_unique<World>(next_project);
                next_world->set_scene_prepare(prepare_scene);
                auto next_assets = std::make_unique<AssetRenderer>(next_world->assets());
                for(auto&[id,scene]:next_project.scenes){UiRenderer checked(*scene,options.executable_directory,true);}
                auto next_ui=std::make_unique<UiRenderer>(next_project,options.executable_directory);
                auto next_audio=std::make_unique<Audio>(options.audio,next_project.document.value("audio",Json::object()));
                project = std::move(next_project); world = std::move(next_world);
                assets = std::move(next_assets);
#ifdef LANGMAKE_MOBILE
                if(mobile){mobile->reset(ui.get());mobile->configure(project.document.value("input",Json::object()));}
#endif
                ui=std::move(next_ui);audio=std::move(next_audio);pending_input_events=Json::array();
                start_music(true); update_audio();
                reset_camera();
                error.clear(); runtime_error_text.clear(); rejected_revision.clear(); selected.clear();
                previous_collected = 0; previous_won = false;
                accumulator = 0;
                reload_status = "RELOADED  /  SIMULATION RESTARTED";
                SDL_SetWindowTitle(window, ("Langmake | " + project.title).c_str());
                return true;
            } catch (const std::exception& e) { error = e.what(); reload_status = "RELOAD FAILED  /  LAST GOOD SCENE RETAINED"; return false; }
        };
        auto request_capture = [&](std::filesystem::path path, std::filesystem::path response = {}, std::string id = {}) {
            // bgfx accepts one screenshot per swapchain per frame. Leave additional
            // control requests queued so every acknowledged request gets an image.
            if (capture_frame == frame) return false;
            path = std::filesystem::absolute(path);
            std::filesystem::create_directories(path.parent_path());
            Json metadata = snapshot();
            metadata["image"] = path.string(); metadata["width"] = width; metadata["height"] = height;
            metadata["render_frame"] = frame;
            metadata["overlay"]=overlay;
            if(options.steps>=0)metadata["scenario"]=revision_for(options.timeline.dump()+":"+std::to_string(options.steps));
            metadata["camera_target"] = camera_target;
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.generic_string().c_str());
            if (std::getenv("LANGMAKE_RENDER_TRACE")) std::fprintf(stderr, "capture requested: %s at frame %d\n", path.string().c_str(), frame);
            pending.push_back({path, std::move(metadata), response, std::move(id)});
            capture_frame = frame;
            return true;
        };
        auto sync_scene = [&] {
            if(prepared_assets){
#ifdef LANGMAKE_MOBILE
                if(mobile)mobile->reset(ui.get());
#endif
                assets=std::move(prepared_assets);ui=std::move(prepared_ui);audio=std::move(prepared_audio);project=world->current_project();start_music();reset_camera();
#ifdef LANGMAKE_MOBILE
                if(mobile)mobile->configure(project.document.value("input",Json::object()));
#endif
            }
        };
        auto slot_path = [&](const std::string& slot) {
            if(slot.empty()||slot.size()>64||slot.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid save slot");
            const auto root=std::filesystem::weakly_canonical(storage);
            const auto target=std::filesystem::weakly_canonical(root/"saves"/(slot+".json"));
            const auto relative=target.lexically_relative(root);
            if(relative.empty()||relative.is_absolute()||std::any_of(relative.begin(),relative.end(),[](const auto& part){return part=="..";}))throw std::runtime_error("Save path escapes runtime storage");
            return target;
        };
        // Opt-in recovery uses only completed snapshots, never the partially
        // mutated world left by an interrupted script update.
        Json safe_script_save=world->supports_recovery()?world->save():Json();
        Json script_failure;unsigned recovery_failures=0,healthy_steps=0;
        auto game_requests = [&] {
            for(auto& request:world->take_requests()) {
                try {
                    auto path=slot_path(request.at("slot").get<std::string>());
                    if(request.at("action")=="save"){auto saved=world->save();write_json_atomic(path,saved);if(world->supports_recovery())safe_script_save=std::move(saved);}
                    else {world->restore(read_json_shared(path,16*1024*1024));if(world->supports_recovery())safe_script_save=world->save();sync_scene();ui->reset_input();pending_input_events=Json::array();start_music();}
                }catch(const std::exception& e){error=e.what();}
            }
        };
        auto refresh_viewport = [&] {
            const float display_density=SDL_GetWindowDisplayScale(window);
            const float density=options.density>0?options.density:display_density>0?display_density:1.f;
            SDL_Rect safe{0,0,width,height};int logical_width=width,logical_height=height;SDL_GetWindowSize(window,&logical_width,&logical_height);
            if(SDL_GetWindowSafeArea(window,&safe)){const float sx=float(width)/std::max(1,logical_width),sy=float(height)/std::max(1,logical_height);safe={int(safe.x*sx),int(safe.y*sy),int(safe.w*sx),int(safe.h*sy)};}
            else safe={0,0,width,height};
            const auto area=options.safe_area!=std::array<int,4>{}?options.safe_area:std::array<int,4>{safe.x,safe.y,safe.w,safe.h};
            ui->set_safe_area(area);
            world->set_viewport(width,height,density,area);
            return density;
        };
        auto refresh_ui = [&] {
            const float density=refresh_viewport();
            ui->update(world->presentation(),world->time(),width,height,density);
        };
        refresh_ui();
        auto step = [&](Input input) {
            try { refresh_viewport();world->step(1.0f / 60.0f, input);sync_scene();game_requests();refresh_ui();update_audio();if(++healthy_steps>=600)recovery_failures=0; }
            catch (const std::exception& e) {
                error = runtime_error_text = e.what(); paused = true; reload_status = "SCRIPT FAILED  /  PAUSED";
                healthy_steps=0;script_failure={{"diagnostic",error},{"tick",world->snapshot().value("tick",0)},{"recovered",false}};
                // Avoid a recursive recovery loop if the recovery UI itself fails.
                if(!safe_script_save.is_null() && recovery_failures++<2)try {
                    world->restore(safe_script_save,true);sync_scene();ui->reset_input();
                    pending_input_events=Json::array();start_music();refresh_ui();
                    script_failure["recovered"]=true;error.clear();runtime_error_text.clear();
                    paused=false;reload_status="RESTORED SAFE SAVE / AWAITING PLAYER";
                }catch(const std::exception& recovery_error){error+=std::string("; recovery failed: ")+recovery_error.what();}

                try{write_json_atomic(bridge/"script-failure.json",script_failure);}catch(...){}
                if(paused&&!safe_script_save.is_null()&&options.steps<0&&!options.frames){
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Game interrupted","Please reopen the game. Your saved progress has been kept.",window);running=false;
                }
            }
        };
#ifdef LANGMAKE_MOBILE
        auto save_recovery=[&] {
            if(options.frames||options.steps>=0||!recovery_writable||!runtime_error_text.empty())return;
            try{write_json_atomic(slot_path(recovery_slot),world->save());recovery_status="saved";}
            catch(const std::exception& exception){recovery_status=std::string("Recovery save failed: ")+exception.what();}
        };
        auto mobile_viewport=[&] {
            if(!mobile)return;
            SDL_Rect safe{0,0,width,height};int logical_width=width,logical_height=height;SDL_GetWindowSize(window,&logical_width,&logical_height);
            if(SDL_GetWindowSafeArea(window,&safe)){const float sx=float(width)/std::max(1,logical_width),sy=float(height)/std::max(1,logical_height);safe={int(safe.x*sx),int(safe.y*sy),int(safe.w*sx),int(safe.h*sy)};}
            else safe={0,0,width,height};
            if(options.safe_area!=std::array<int,4>{})safe={options.safe_area[0],options.safe_area[1],options.safe_area[2],options.safe_area[3]};
            const float density=options.density>0?options.density:SDL_GetWindowDisplayScale(window);
            mobile->resize(width,height,density>0?density:1.f,safe,ui.get());
            world->set_viewport(width,height,density>0?density:1.f,{safe.x,safe.y,safe.w,safe.h});
            ui->set_safe_area({safe.x,safe.y,safe.w,safe.h});
            ui->update(world->presentation(),world->time(),width,height,density>0?density:1.f);
        };
        mobile_viewport();
        std::uint64_t handled_background_epoch=0;
#endif

#ifdef LANGMAKE_IOS
        // iOS can suspend the process immediately after this callback. Persist
        // and stop graphics/audio synchronously, before yielding to UIKit.
        events->lifecycle=[&](const SDL_Event& event){
            auto copy=event;MobileLifecycle::event(&lifecycle,&copy);
            if(event.type==SDL_EVENT_WILL_ENTER_BACKGROUND||event.type==SDL_EVENT_TERMINATING){
                if(mobile)mobile->reset(ui.get());
                audio->set_paused(true);audio->set_suspended(true);
                accumulator=0;jump_pending=false;pending_input_events=Json::array();
                save_recovery();destroy_graphics();
                ++publication_sequence;publications.latest(live_path,snapshot());
            }
        };
        struct ClearLifecycle { PreviewEvents* events;~ClearLifecycle(){events->lifecycle={};} } clear_lifecycle{events};
#endif
        while (running) {
            co_await std::suspend_always{};
            if(events->stop){running=false;
#ifdef LANGMAKE_MOBILE
                save_recovery();
#endif
                break;
            }
            if(!audio->pump_device())error="Audio device could not recover after interruption; game remains playable without audio";
            const auto now = std::chrono::steady_clock::now();
            if(frame>5){frame_samples.push_back(std::chrono::duration<float,std::milli>(now-last_time).count());if(frame_samples.size()>4096)frame_samples.pop_front();}
            const float dt = std::min(.1f, std::chrono::duration<float>(now - last_time).count());
            last_time = now;
            frame_ms = frame_ms * .94f + dt * 1000 * .06f;
            SDL_Event event;
            bool click = false; float mouse_x = 0, mouse_y = 0;
            while (events->next(event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
#ifdef LANGMAKE_MOBILE
                if(lifecycle.background||!renderer_ready)continue;
                if(!options.isolated&&mobile&&ui&&mobile->process_event(event,*ui))continue;
                if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST&&mobile)mobile->reset(ui.get());
                if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&event.key.scancode==SDL_SCANCODE_AC_BACK)event.key.key=SDLK_ESCAPE;
#endif
                if(!options.isolated&&ui&&ui->process_event(event))continue;
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !options.isolated) {
                    // Exit is a player action too. UI gets first refusal above (e.g. cancel focus).
                    if(event.key.scancode==SDL_SCANCODE_ESCAPE||event.key.scancode==SDL_SCANCODE_AC_BACK){running=false;continue;}
                    if(event.key.scancode==SDL_SCANCODE_SPACE)jump_pending=true;
                    if(options.player)continue;
                    switch (event.key.scancode) {
                    case SDL_SCANCODE_P: paused = !paused; if(paused)ui->reset_input();accumulator = 0; break;
                    case SDL_SCANCODE_R: case SDL_SCANCODE_F5: reload(); break;
                    case SDL_SCANCODE_SPACE: jump_pending = true; break;
                    case SDL_SCANCODE_F2: overlay = !overlay; break;
                    case SDL_SCANCODE_F: follow = !follow; developer_camera=true; break;
                    case SDL_SCANCODE_F12: capture_key = true; break;
                    default: break;
                    }
                }
                if (!options.player && !options.isolated && event.type == SDL_EVENT_MOUSE_WHEEL) {zoom = std::clamp(zoom * std::pow(.9f, event.wheel.y), .35f, 2.5f);developer_camera=true;}
                if (!options.player && !options.isolated && event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.state & SDL_BUTTON_RMASK)) {
                    yaw -= event.motion.xrel * .005f; pitch = std::clamp(pitch + event.motion.yrel * .005f, .15f, 1.35f);
                    developer_camera=true;
                }
                if (!options.player && !options.isolated && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                    click = true; mouse_x = event.button.x; mouse_y = event.button.y;
                }
                if (event.type == SDL_EVENT_GAMEPAD_ADDED && !controller) controller = SDL_OpenGamepad(event.gdevice.which);
                if (event.type == SDL_EVENT_GAMEPAD_REMOVED && controller && SDL_GetGamepadID(controller) == event.gdevice.which) { SDL_CloseGamepad(controller); controller = nullptr; }
            }
#ifdef LANGMAKE_MOBILE
            if(lifecycle.terminating)running=false;
            if(!running){save_recovery();break;}
            bool surface_changed=!renderer_ready;
#ifdef __ANDROID__
            surface_changed=surface_changed||!native_surface.matches(window);
#endif
            if(lifecycle.background||handled_background_epoch!=lifecycle.background_epoch.load()||surface_changed) {
                audio->set_paused(true);if(!audio->set_suspended(true))error="Could not suspend mobile audio device";
                accumulator=0;jump_pending=false;pending_input_events=Json::array();
                if(renderer_ready){save_recovery();destroy_graphics();}
                handled_background_epoch=lifecycle.background_epoch.load();
                bool unavailable=lifecycle.background;
#ifdef __ANDROID__
                unavailable=unavailable||!SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,nullptr);
#endif
                if(unavailable) {
                    if(std::chrono::duration<double>(now-last_live).count()>.25){++publication_sequence;publications.latest(live_path,snapshot());last_live=now;}
                    if(!events->callbacks)SDL_Delay(30);continue;
                }
                int resumed_width=0,resumed_height=0;SDL_GetWindowSizeInPixels(window,&resumed_width,&resumed_height);
                if(resumed_width<1||resumed_height<1){if(!events->callbacks)SDL_Delay(30);continue;}
                initialize_graphics();mobile_viewport();last_time=std::chrono::steady_clock::now();frame_samples.clear();
                if(!audio->set_suspended(false))error="mobile audio device could not resume; game remains playable without audio";
            }
#endif
            int new_width = 0, new_height = 0; SDL_GetWindowSizeInPixels(window, &new_width, &new_height);
            if (new_width < 1 || new_height < 1) { if(!events->callbacks)SDL_Delay(30); continue; }
            if (new_width != width || new_height != height) {
                width = new_width; height = new_height;
                bgfx::SwapChain swap_chain;
                swap_chain.width = width; swap_chain.height = height;
#ifndef LANGMAKE_MOBILE
                swap_chain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
#else
                if(mobile)mobile->reset(ui.get());
#endif
                bgfx::reset(BGFX_RESET_VSYNC, &swap_chain);
            }
#ifdef LANGMAKE_MOBILE
            mobile_viewport();
#endif

            if (options.watch_source && !std::filesystem::exists(project.root / ".langmake/mutation.lock") && std::chrono::duration<double>(now - last_poll).count() > .3) {
                last_poll = now;
                try {
                    auto observed = load_project(project.file,project.profile);
                    if (observed.revision != project.revision && observed.revision != rejected_revision) {
                        auto attempted = observed.revision;
                        if (!reload()) rejected_revision = attempted;
                    } else if (observed.revision == project.revision && (reload_status.starts_with("INVALID SOURCE") || reload_status.starts_with("RELOAD FAILED"))) {
                        error.clear(); rejected_revision.clear(); reload_status = "SOURCE RESTORED  /  LAST GOOD SCENE RUNNING";
                    }
                } catch (const std::exception& e) { error = e.what(); reload_status = "INVALID SOURCE  /  LAST GOOD SCENE RETAINED"; }
            }

            const bool* keys = SDL_GetKeyboardState(nullptr);
            Input input;
            input.move_x = float(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) - float(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
            input.move_z = float(keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) - float(keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]);
            input.jump = jump_pending;
            input.fire = (keys[SDL_SCANCODE_SPACE]&&!ui->consumes_key(SDL_SCANCODE_SPACE)) || keys[SDL_SCANCODE_LCTRL];
            bool controller_jump = false;
            if (controller) {
                float x = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                float z = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                if (std::abs(x) > .15f) input.move_x = std::clamp(x, -1.0f, 1.0f);
                if (std::abs(z) > .15f) input.move_z = std::clamp(z, -1.0f, 1.0f);
                controller_jump = SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_SOUTH)&&!ui->consumes_gamepad_button(SDL_GAMEPAD_BUTTON_SOUTH);
                input.jump = input.jump || controller_jump;
                input.fire = input.fire || SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_WEST);
            }
            const auto& input_config=world->current_project().document.value("input",Json::object());
            if(input_config.contains("actions"))for(auto&[name,binding]:input_config["actions"].items()){
                float value=0;
                auto key_down=[&](const Json& list){bool down=false;for(auto& key:list){auto code=SDL_GetScancodeFromName(key.get<std::string>().c_str());if(code!=SDL_SCANCODE_UNKNOWN&&!ui->consumes_key(code))down=down||keys[code];}return down;};
                if(binding.contains("keys"))value=key_down(binding["keys"])?1.f:0.f;
                if(binding.contains("positive"))value+=key_down(binding["positive"])?1.f:0.f;
                if(binding.contains("negative"))value-=key_down(binding["negative"])?1.f:0.f;
                if(controller&&binding.contains("gamepad")){auto name=binding["gamepad"].get<std::string>();
                    if(name=="south")name="a";else if(name=="east")name="b";else if(name=="west")name="x";else if(name=="north")name="y";
                    auto button=SDL_GetGamepadButtonFromString(name.c_str());if(button!=SDL_GAMEPAD_BUTTON_INVALID&&!ui->consumes_gamepad_button(button)&&SDL_GetGamepadButton(controller,button))value=1;}
                if(controller&&binding.contains("gamepad_axis")){auto axis=SDL_GetGamepadAxisFromString(binding["gamepad_axis"].get<std::string>().c_str());if(axis!=SDL_GAMEPAD_AXIS_INVALID){float v=SDL_GetGamepadAxis(controller,axis)/32767.f;if(std::abs(v)>.15f)value=v;}}
                input.actions[name]=std::clamp(value,-1.f,1.f);
            }
            float pointer_x=0,pointer_y=0;SDL_GetMouseState(&pointer_x,&pointer_y);int logical_w=1,logical_h=1;SDL_GetWindowSize(window,&logical_w,&logical_h);
            input.pointer={std::clamp(pointer_x/std::max(1,logical_w),0.f,1.f),std::clamp(pointer_y/std::max(1,logical_h),0.f,1.f)};
            for(auto& event:ui->drain_events())if(pending_input_events.size()<256)pending_input_events.push_back(event);
            if(ui->captures_keyboard())input=Input{};
            Input hardware_input=input;
            if(!options.isolated)ui->apply(input);
#ifdef LANGMAKE_MOBILE
            if(mobile&&!options.isolated)mobile->apply(input);
#endif
            input.events=pending_input_events;
            if(options.steps>=0) {
                // A requested tick stays fixed until its pixels and sidecar finish.
                if(pending.empty() && scenario_tick<options.steps &&
                    (next_capture_tick>=options.capture_ticks.size() || scenario_tick!=options.capture_ticks[next_capture_tick])) {
                    step(timeline.at(scenario_tick));++scenario_tick;
                }
            } else if (!paused && (options.frames == 0 || frame < options.frames)) {
                accumulator += options.frames > 0 ? 1.0 / 60.0 : dt;
                int steps = 0;
                const auto catchup_started = std::chrono::steady_clock::now();
                while (accumulator >= 1.0 / 60.0 && steps++ < 6 && !paused) {
                    input=hardware_input;input.events=pending_input_events;
                    if(!options.isolated)ui->apply(input);
#ifdef LANGMAKE_MOBILE
                    if(mobile&&!options.isolated)mobile->apply(input);
#endif
                    step(input); accumulator -= 1.0 / 60.0; jump_pending = false;hardware_input.jump=controller_jump;pending_input_events=Json::array();input.events=Json::array();ui->consume_tick();
#ifdef LANGMAKE_MOBILE
                    if(mobile)mobile->consume_tick();
#endif
                    // An overloaded simulation must still present and consume new
                    // input. Never spend six expensive ticks hiding the next frame.
                    // Fixed-frame/scenario verification retains exact tick behavior.
                    if(options.frames==0 && std::chrono::duration<double>(std::chrono::steady_clock::now()-catchup_started).count()>=1.0/60.0) {
                        accumulator=std::min(accumulator,1.0/60.0);
                        break;
                    }
                }
            }

            if(options.prepare_commands&&!options.isolated)try{options.prepare_commands(bridge);}catch(const std::exception& exception){error=std::string("Device command transport: ")+exception.what();}
            std::vector<std::filesystem::path> requests;
            for (const auto& entry : std::filesystem::directory_iterator(bridge / "commands"))
                if (entry.is_regular_file() && entry.path().extension() == ".json") requests.push_back(entry.path());
            std::sort(requests.begin(), requests.end());
            for (const auto& request_path : requests) {
                if(options.isolated || !publications.available() || handled_requests.size()>=65536)break;
                auto id = request_path.stem().string();
                if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) continue;
                auto response_path = bridge / "results" / (id + ".json");
                if (handled_requests.contains(id) || std::filesystem::exists(response_path)) { std::error_code ignored;std::filesystem::remove(request_path,ignored);continue; }
                // Record completion independently of receipt delivery; a lock on results cannot replay a mutation.
                handled_requests[id]=wall_time_ms()+600000;
                Json receipt;
                try {
                    if (std::filesystem::file_size(request_path) > 16384) throw std::runtime_error("Control request exceeds 16 KiB");
                    auto request = read_json(request_path);
                    if (request.at("id") != id || request.at("session") != session) throw std::runtime_error("Control request belongs to a different preview session");
                    if(request.value("expires_at_ms",wall_time_ms()+10000)<wall_time_ms())throw std::runtime_error("Control request expired before execution");
                    auto action = request.at("action").get<std::string>();
                    if(options.player && action!="snapshot" && action!="capture" && action!="save" && action!="load")throw std::runtime_error("Player mode accepts observation and saves only; use preview for developer controls");
                    if (action == "pause") {paused = true;ui->reset_input();}
                    else if (action == "resume") paused = false;
                    else if (action == "reset") { if (!reload()) throw std::runtime_error(error); }
                    else if(action=="save")write_json_atomic(slot_path(request.value("slot",std::string("quick"))),world->save());
                    else if(action=="load"){world->restore(read_json_shared(slot_path(request.value("slot",std::string("quick"))),16*1024*1024));sync_scene();ui->reset_input();pending_input_events=Json::array();start_music();}
                    else if (action == "step") {
                        paused = true;ui->reset_input();
                        int count = request.value("steps", 1);
                        if (count < 0 || count > 600) throw std::runtime_error("Control steps must be 0..600");
                        Input controlled=request.contains("input")?parse_input(request["input"]):Input{};
                        try { for (int i = 0; i < count; ++i) { world->step(1.0f / 60.0f, controlled);sync_scene();game_requests();update_audio(); } }
                        catch (const std::exception& e) {
                            error = runtime_error_text = e.what(); reload_status = "SCRIPT FAILED  /  PAUSED"; throw;
                        }
                    } else if (action == "capture") {
                        if (request_capture(bridge / "evidence" / (id + ".png"), response_path, id)) {
                            std::filesystem::remove(request_path);
                            // Later step/reset commands belong to the next frame;
                            // they must not change the pixels for this snapshot.
                            break;
                        }
                        continue;
                    } else if (action != "snapshot") throw std::runtime_error("Unknown control action");
                    accumulator = 0;
                    auto state=snapshot();
                    receipt={{"ok",true},{"api_version",2},{"id",id},{"session",session},{"action",action},{"tick",state["tick"]},{"source_revision",project.revision},{"paused",paused}};
                    if(action=="snapshot"||request.value("include_state",false))receipt["state"]=filter_observation(std::move(state),request.value("query",Json::object()));
                } catch (const std::exception& e) { receipt={{"ok", false}, {"id", id}, {"diagnostic", e.what()}}; }
                publications.receipt(response_path,std::move(receipt));
                std::error_code ignored;std::filesystem::remove(request_path,ignored);
            }

            auto entities = world->entities();
            audio->set_paused(paused);
            if (project.assets.empty() && world->collected() > previous_collected) audio->play(660 + world->collected() * 110);
            if (project.assets.empty() && world->won() && !previous_won) audio->play(1046, .6f);
            previous_collected = world->collected(); previous_won = world->won();
            if (follow) for (const auto& e : entities) if (e.role == "player") {
                for (int i = 0; i < 3; ++i) camera_target[i] += (e.position[i] - camera_target[i]) * .07f;
            }
            bool flat = project.dimension == "2d";
            const bool homogeneous = bgfx::getCaps()->homogeneousDepth;
            bx::Vec3 eye{0,0,0};
            auto presentation=world->presentation();
            const auto runtime_camera=presentation.value("camera",Json::object());
            const bool game_camera=runtime_camera.value("enabled",false)&&!developer_camera;
            if(game_camera)camera_target=runtime_camera.at("target").get<Vec3>();
            if (flat || (game_camera&&runtime_camera.value("mode",std::string())=="orthographic")) {
                eye = {camera_target[0], camera_target[1], camera_target[2] + 40};
                float half_height = game_camera?runtime_camera.value("half_height",11.f):11.0f*zoom;
                float half_width = half_height * width / height;
                bx::mtxOrtho(projection, -half_width, half_width, -half_height, half_height, .1f, 200, 0, homogeneous, bx::Handedness::Right);
            } else {
                const float distance = camera_distance * zoom;
                eye = {camera_target[0] + std::sin(yaw) * std::cos(pitch) * distance,
                    camera_target[1] + std::sin(pitch) * distance,
                    camera_target[2] + std::cos(yaw) * std::cos(pitch) * distance};
                bx::mtxProj(projection, game_camera?runtime_camera.value("fov",48.f):48.f, float(width) / height, .1f, 300, homogeneous, bx::Handedness::Right);
            }
            if(game_camera&&!flat){auto pos=runtime_camera.at("position").get<Vec3>();eye={pos[0],pos[1],pos[2]};}
            bx::mtxLookAt(view, eye, {camera_target[0],camera_target[1],camera_target[2]}, {0,1,0}, bx::Handedness::Right);
            bx::mtxMul(view_projection, view, projection);
            bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
            bgfx::setViewTransform(0, view, projection);
            bgfx::touch(0);
            std::vector<Vertex> vertices; std::vector<uint16_t> indices;
            // Submit in bounded batches; persistent object identities never depend on vertex offsets.
            for (const auto& entity : entities) {
                if (!entity.visible || !entity.asset.empty()) continue;
                if (vertices.size() > 50000) { submit_mesh(vertices, indices, layout, program, 0, true); vertices.clear(); indices.clear(); }
                auto visual=entity;for(int axis=0;axis<3;++axis)visual.size[axis]*=visual.visual_scale[axis];
                const auto start=vertices.size();
                if (visual.shape == "sphere"||visual.shape=="capsule") sphere_mesh(vertices, indices, visual, flat);
                else box_mesh(vertices, indices, visual, flat);
                if(visual.rotation!=Vec3{0,0,0}){
                    float rotation[16];bx::mtxRotateXYZ(rotation,visual.rotation[0]*pi/180,visual.rotation[1]*pi/180,visual.rotation[2]*pi/180);
                    for(size_t v=start;v<vertices.size();++v){auto& point=vertices[v];const float x=point.x-visual.position[0],y=point.y-visual.position[1],z=point.z-visual.position[2];
                        point.x=visual.position[0]+rotation[0]*x+rotation[4]*y+rotation[8]*z;point.y=visual.position[1]+rotation[1]*x+rotation[5]*y+rotation[9]*z;point.z=visual.position[2]+rotation[2]*x+rotation[6]*y+rotation[10]*z;}
                }
            }
            submit_mesh(vertices, indices, layout, program, 0, true);
            assets->sync_surfaces(world->surfaces());
            assets->draw(entities, program, flat, {eye.x,eye.y,eye.z});
            if(!options.debug.empty())debug_lines(entities,world->snapshot(),options.debug,layout,program);
            refresh_ui();ui->draw();
#ifdef LANGMAKE_MOBILE
            if(mobile&&!options.isolated)mobile->draw(world->time());
#endif

            if (click) {
                int logical_width, logical_height; SDL_GetWindowSize(window, &logical_width, &logical_height);
                mouse_x *= float(width) / logical_width; mouse_y *= float(height) / logical_height;
                float best = 50;
                selected.clear();
                for (const auto& e : entities) if (e.visible && e.size[0] < 15) {
                    auto p = e.position;
                    float w = p[0] * view_projection[3] + p[1] * view_projection[7] + p[2] * view_projection[11] + view_projection[15];
                    if (w <= 0) continue;
                    float x = (p[0]*view_projection[0]+p[1]*view_projection[4]+p[2]*view_projection[8]+view_projection[12])/w;
                    float y = (p[0]*view_projection[1]+p[1]*view_projection[5]+p[2]*view_projection[9]+view_projection[13])/w;
                    float distance = std::hypot((x+1)*width*.5f-mouse_x, (1-y)*height*.5f-mouse_y);
                    if (distance < best) { best = distance; selected = e.id; }
                }
            }
            bgfx::dbgTextClear();
            if (overlay) {
                const int columns = std::max(50, width / 8);
                const int rows = height / 16;
                bgfx::dbgTextPrintf(2, 1, 0x0b, "L A N G M A K E   /   0.5.0");
                bgfx::dbgTextPrintf(2, 3, 0x0f, "%s", single_line(project.title, columns - 5).c_str());
                bgfx::dbgTextPrintf(2, 4, 0x08, "%s  |  C++20 + LUAU  |  %s  |  %s", flat ? "2D PLAYGROUND" : "3D PLAYGROUND", flat ? "BOX2D" : "JOLT",bgfx::getRendererName(bgfx::getRendererType()));
                bgfx::dbgTextPrintf(2, 6, world->won() ? 0x0a : 0x0e, "%s", single_line(world->message(), columns - 5).c_str());
                bgfx::dbgTextPrintf(2, 7, 0x0f, "COLLECTED %d    %s    %.1f ms/frame", world->collected(), paused ? "PAUSED" : "PLAYING", frame_ms);
                bgfx::dbgTextPrintf(2, rows - 5, error.empty() ? 0x08 : 0x0c, "%s", single_line(error.empty() ? reload_status : error, columns - 5).c_str());
                bgfx::dbgTextPrintf(2, rows - 3, 0x07, "A/D ARROWS: MOVE   W/S: DEPTH   SPACE: JUMP/FIRE   P: PAUSE   R: RESET   F5: RELOAD");
                bgfx::dbgTextPrintf(2, rows - 2, 0x08, "RMB: ORBIT   WHEEL: ZOOM   F: FOLLOW   CLICK: INSPECT   F2: HUD   F12: CAPTURE   ESC: EXIT");
                if (!selected.empty()) for (const auto& e : entities) if (e.id == selected) {
                    bgfx::dbgTextPrintf(2, 9, 0x0b, "SELECTED  %s  [%s]", single_line(e.id, 40).c_str(), e.body.c_str());
                    bgfx::dbgTextPrintf(2, 10, 0x07, "position  %.2f  %.2f  %.2f", e.position[0], e.position[1], e.position[2]);
                }
            }

            if (capture_key) capture_key = !request_capture(bridge / "evidence" / ("capture-" + session + "-" + std::to_string(frame) + ".png"));
            if(options.steps>=0 && pending.empty() && next_capture_tick<options.capture_ticks.size() && scenario_tick==options.capture_ticks[next_capture_tick]) {
                if(request_capture(options.capture_directory/("tick-"+std::to_string(scenario_tick)+".png")))++next_capture_tick;
            }
            if (options.steps<0 && options.frames > 0 && frame >= options.frames - 1 && !options.capture.empty() && !requested_final_capture)
                requested_final_capture = request_capture(options.capture);
            for (auto& capture : pending) if (capture.metadata["render_frame"] == frame) {
                capture.metadata["camera_target"] = camera_target;
                capture.metadata["camera_position"] = {eye.x, eye.y, eye.z};
                capture.metadata["view_matrix"] = std::vector<float>(view, view + 16);
                capture.metadata["projection_matrix"] = std::vector<float>(projection, projection + 16);
            }
            bgfx::frame();
            {
                std::lock_guard guard(callbacks.mutex);
                for (auto it = pending.begin(); it != pending.end();) {
                    auto result = callbacks.captures.find(it->path.generic_string());
                    if (result == callbacks.captures.end()) { ++it; continue; }
                    bool ok = result->second.empty();
                    if (ok) { try { auto sidecar = it->path; sidecar += ".json"; write_json_atomic(sidecar, it->metadata); completed.push_back(it->path.string()); } catch(const std::exception& e){ok=false;result->second=e.what();} }
                    if(!ok)error=result->second;
                    if (!it->response.empty()) publications.receipt(it->response, {{"ok", ok}, {"id", it->request_id}, {"image", it->path.string()}, {"source_revision", it->metadata["source_revision"]}, {"diagnostic", result->second}});
                    callbacks.captures.erase(result); it = pending.erase(it);
                }
            }
            if (std::chrono::duration<double>(now - last_live).count() > .25 || frame == 0) { ++publication_sequence;publications.latest(live_path,snapshot());last_live=now; }
            if(frame%300==0) try {
                const auto current=wall_time_ms();
                for(auto it=handled_requests.begin();it!=handled_requests.end();)if(it->second<current)it=handled_requests.erase(it);else ++it;
                std::vector<std::filesystem::directory_entry> results;uintmax_t bytes=0;
                for(auto& entry:std::filesystem::directory_iterator(bridge/"results"))if(entry.is_regular_file()&&entry.path().extension()==".json"){results.push_back(entry);bytes+=entry.file_size();}
                std::sort(results.begin(),results.end(),[](const auto&a,const auto&b){return a.last_write_time()<b.last_write_time();});
                size_t count=results.size();
                for(auto& entry:results)if(count>1024||bytes>16*1024*1024||std::filesystem::file_time_type::clock::now()-entry.last_write_time()>std::chrono::minutes(10)){std::error_code ignored;auto size=entry.file_size();if(std::filesystem::remove(entry.path(),ignored)){--count;bytes-=size;}}
            }
            catch(const std::exception& e){error=std::string("Receipt maintenance: ")+e.what();}
            ++frame;
            if(options.steps>=0) {
                if(scenario_tick>=options.steps&&next_capture_tick==options.capture_ticks.size()&&pending.empty())running=false;
                if(frame>options.steps+int(options.capture_ticks.size())*180+180)throw std::runtime_error("Timeline capture timed out");
            } else {
                if (options.frames > 0 && frame >= options.frames + 3 && pending.empty() && (options.capture.empty() || requested_final_capture)) running = false;
                if (options.frames > 0 && frame >= options.frames + 180 && !pending.empty()) throw std::runtime_error("GPU screenshot timed out");
            }
        }
        if (!options.capture.empty() && std::find(completed.begin(), completed.end(), std::filesystem::absolute(options.capture).string()) == completed.end())
            throw std::runtime_error("Requested capture did not complete: " + error);
        if(options.steps>=0 && completed.size()!=options.capture_ticks.size())throw std::runtime_error("Timeline evidence incomplete: "+error);
        if ((options.frames > 0 || options.steps>=0) && !runtime_error_text.empty()) throw std::runtime_error(runtime_error_text);
        final_state = snapshot(); final_state["running"] = false;
        publications.latest(live_path,final_state);
        destroy_graphics();
#ifdef LANGMAKE_MOBILE
        lifecycle.stop();
#endif
        events->lifecycle={};
        if (controller) SDL_CloseGamepad(controller);
#if defined(__APPLE__) && !defined(LANGMAKE_IOS)
        if(metal_view){SDL_Metal_DestroyView(metal_view);metal_view=nullptr;}
#endif
        SDL_DestroyWindow(window); SDL_Quit();
        co_return Json{{"ok", true}, {"frames", frame}, {"captures", completed}, {"state", final_state}};
    } catch (...) {
#ifdef LANGMAKE_MOBILE
        mobile.reset();lifecycle.stop();
#endif
        if (renderer_ready) { ui.reset();assets.reset(); if (bgfx::isValid(program)) bgfx::destroy(program); bgfx::shutdown(); }
#ifdef LANGMAKE_MOBILE
        native_surface.reset();
#endif
        events->lifecycle={};
        if (controller) SDL_CloseGamepad(controller);
#if defined(__APPLE__) && !defined(LANGMAKE_IOS)
        if(metal_view){SDL_Metal_DestroyView(metal_view);metal_view=nullptr;}
#endif
        SDL_DestroyWindow(window); SDL_Quit();
        publications.latest(live_path, {{"running", false}, {"session", session}, {"source_revision", project.revision},{"published_at_ms",wall_time_ms()}});
        throw;
    }
#endif
}
struct PreviewSession::Impl {
    PreviewEvents events;
    PreviewTask task;
    bool started=false;
    Impl(Project project,PreviewOptions options,bool callbacks):task(preview_frames(std::move(project),std::move(options),&events)){events.callbacks=callbacks;}
};
PreviewSession::PreviewSession(Project project,PreviewOptions options,bool callback_events):impl_(std::make_unique<Impl>(std::move(project),std::move(options),callback_events)){}
PreviewSession::~PreviewSession(){try{stop();}catch(...){}}
bool PreviewSession::iterate(){
    auto& self=*impl_;self.started=true;
    if(!self.task.handle.done())self.task.handle.resume();
    if(self.task.handle.promise().error)std::rethrow_exception(self.task.handle.promise().error);
    return !self.task.handle.done();
}
void PreviewSession::event(const SDL_Event& event){if(!impl_->task.handle.done())impl_->events.push(event);}
void PreviewSession::stop(){if(impl_->started&&!impl_->task.handle.done()){impl_->events.stop=true;iterate();}}
Json PreviewSession::result()const{
    if(!impl_->task.handle.done())throw std::runtime_error("Preview session is still running");
    if(impl_->task.handle.promise().error)std::rethrow_exception(impl_->task.handle.promise().error);
    return impl_->task.handle.promise().result;
}
Json preview(Project project,const PreviewOptions& options){
    PreviewSession session(std::move(project),options);
    while(session.iterate()){}
    return session.result();
}
}
