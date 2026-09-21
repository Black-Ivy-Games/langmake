#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "langmake/preview.hpp"
#include "langmake/project.hpp"
#include "langmake/io.hpp"
#include "langmake/ios_contract.hpp"
#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <set>
#include <stdexcept>

namespace {
using Path=std::filesystem::path;
using lm::Json;
using lm::ios::contained;
struct Application {
    Path storage;
    std::unique_ptr<lm::PreviewSession> session;
    bool finished=false;
};
std::string sha256(const Path& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot read bundled content: "+path.filename().string());
    CC_SHA256_CTX context;CC_SHA256_Init(&context);
    std::array<char,65536> buffer;
    while(stream){stream.read(buffer.data(),buffer.size());const auto count=stream.gcount();if(count>0)CC_SHA256_Update(&context,buffer.data(),static_cast<CC_LONG>(count));}
    if(!stream.eof())throw std::runtime_error("Could not finish reading bundled content");
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256_Final(digest,&context);
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(unsigned char byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}
    return result;
}
Json manifest(const Path& root) {
    auto result=lm::read_json_shared(root/"manifest.json",16*1024*1024);
    if(result.value("format_version",0)!=1||result.value("target",std::string{})!="ios")throw std::runtime_error("Unsupported iOS content manifest");
    if(result.value("project_manifest",std::string{})!="project/project.json")throw std::runtime_error("iOS manifest must name project/project.json");
    const auto identity=result.at("storage_id").get<std::string>();
    if(identity.empty()||identity.size()>64||identity.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
        throw std::runtime_error("Invalid iOS project storage identity");
    const auto bundle=result.at("bundle_identifier").get<std::string>();
    const char* actual=[NSBundle mainBundle].bundleIdentifier.UTF8String;
    if(!actual||bundle!=actual)throw std::runtime_error("Bundled project does not match application identity");
    if(result.value("development",false)!=bool(LANGMAKE_IOS_DEVELOPMENT))throw std::runtime_error("Content development mode differs from the compiled player");
    const auto& files=result.at("files");
    if(!files.is_array()||files.empty()||files.size()>16384)throw std::runtime_error("Invalid iOS content inventory");
    std::set<std::string> names;std::uint64_t total=0;
    for(const auto& entry:files) {
        const auto name=entry.at("path").get<std::string>();
        if(name.empty()||name.find_first_of("\\:")!=std::string::npos||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;})||Path(name).is_absolute()||Path(name).lexically_normal().generic_string()!=name||!names.insert(name).second)
            throw std::runtime_error("Invalid or duplicate iOS manifest path");
        if(!name.starts_with("project/")&&!name.starts_with("runtime/"))throw std::runtime_error("Manifest path is outside project/runtime content");
        const auto file=contained(root,name);
        if(std::filesystem::is_symlink(file)||!std::filesystem::is_regular_file(file))throw std::runtime_error("Manifest resource is not a regular file: "+name);
        const auto bytes=std::filesystem::file_size(file);
        if(bytes!=entry.at("bytes").get<std::uint64_t>()||bytes>256ull*1024*1024)throw std::runtime_error("Bundled resource size mismatch: "+name);
        total+=bytes;if(total>256ull*1024*1024)throw std::runtime_error("iOS content exceeds the initial 256 MiB bundle budget");
        if(sha256(file)!=entry.at("sha256").get<std::string>())throw std::runtime_error("Bundled resource hash mismatch: "+name);
    }
    for(const char* required:{"project/project.json","runtime/shaders/vs_cubes.bin","runtime/shaders/fs_cubes.bin","runtime/fonts/LatoLatin-Regular.ttf"})
        if(!names.contains(required))throw std::runtime_error(std::string("iOS manifest is missing ")+required);
    if(result.value("content_bytes",total)!=total)throw std::runtime_error("iOS content inventory byte count differs");
    return result;
}
Path storage_directory(const Json& content) {
    NSError* error=nil;
    NSURL* application_support=[[NSFileManager defaultManager] URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&error];
    if(!application_support)throw std::runtime_error("iOS Application Support storage is unavailable");
    const Path base(application_support.path.UTF8String);
    const auto result=contained(base,"Langmake/"+content.at("storage_id").get<std::string>());
    std::filesystem::create_directories(result);
    std::filesystem::create_directories(result/"inbox");
    return result;
}
SDL_AppResult failure(Application* app,const std::exception& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Langmake iOS: %s",error.what());
    if(app){app->finished=true;if(!app->storage.empty())try{lm::write_json_atomic(app->storage/"launch-result.json",{{"ok",false},{"running",false},{"version","0.5.0"},{"platform","ios"},{"diagnostic",error.what()}});}catch(...){}}
    return SDL_APP_FAILURE;
}
}

SDL_AppResult SDL_AppInit(void** appstate,int argc,char** argv) {
    auto* app=new Application;*appstate=app;
    try {
        const char* resource=[NSBundle mainBundle].resourcePath.UTF8String;
        if(!resource)throw std::runtime_error("iOS application bundle is unavailable");
        const Path root=Path(resource)/"langmake";const auto content=manifest(root);
        app->storage=storage_directory(content);
        auto configuration=lm::ios::parse_options(argc,argv,root,app->storage,bool(LANGMAKE_IOS_DEVELOPMENT));
        configuration.prepare_commands=[cursor=std::make_shared<lm::ios::InboxCursor>()](const Path& storage){lm::ios::commit_inbox(storage,sha256,*cursor);};
        auto project=lm::load_project(root/"project/project.json");
        if(content.contains("packaged_revision")&&content.at("packaged_revision")!=project.revision)throw std::runtime_error("iOS packaged project revision differs from its manifest");
        SDL_SetAppMetadata(project.title.c_str(),"0.5.0",content.at("bundle_identifier").get<std::string>().c_str());
        const auto revision=project.revision;
        app->session=std::make_unique<lm::PreviewSession>(std::move(project),std::move(configuration),true);
        app->session->iterate(); // Initialize and yield before the first frame.
        lm::write_json_atomic(app->storage/"launch-result.json",{{"ok",true},{"running",true},{"version","0.5.0"},{"platform","ios"},{"source_revision",revision}});
        return SDL_APP_CONTINUE;
    }catch(const std::exception& error){return failure(app,error);}
}
SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app=static_cast<Application*>(appstate);
    try {
        if(app->session->iterate())return SDL_APP_CONTINUE;
        auto result=app->session->result();result["version"]="0.5.0";result["platform"]="ios";
        lm::write_json_atomic(app->storage/"launch-result.json",result);app->finished=true;
        return SDL_APP_SUCCESS;
    }catch(const std::exception& error){return failure(app,error);}
}
SDL_AppResult SDL_AppEvent(void* appstate,SDL_Event* event) {
    auto* app=static_cast<Application*>(appstate);
    try {if(app&&app->session)app->session->event(*event);return SDL_APP_CONTINUE;}
    catch(const std::exception& error){return failure(app,error);}
}
void SDL_AppQuit(void* appstate,SDL_AppResult) {
    std::unique_ptr<Application> app(static_cast<Application*>(appstate));
    if(!app||!app->session)return;
    try {
        app->session->stop();
        if(!app->finished){auto result=app->session->result();result["version"]="0.5.0";result["platform"]="ios";lm::write_json_atomic(app->storage/"launch-result.json",result);}
    }catch(const std::exception& error){if(!app->finished)failure(app.get(),error);}
}
