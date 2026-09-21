#pragma once
#include "preview.hpp"
#include "io.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>

namespace lm::ios {
using Path=std::filesystem::path;
inline Path contained(const Path& root,const std::string& value) {
    const auto base=std::filesystem::weakly_canonical(root);
    Path input(value);
    const auto path=std::filesystem::weakly_canonical(input.is_absolute()?input:base/input);
    const auto relative=path.lexically_relative(base);
    if(relative.empty()||relative=="."||relative.is_absolute()||std::any_of(relative.begin(),relative.end(),[](const auto& item){return item=="..";}))
        throw std::runtime_error("iOS path escapes its designated application directory");
    return path;
}
inline int integer(const std::string& value,int minimum,int maximum) {
    int result=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc()||parsed.ptr!=value.data()+value.size()||result<minimum||result>maximum)
        throw std::runtime_error("iOS numeric argument is outside its supported range");
    return result;
}
inline lm::PreviewOptions parse_options(int argc,const char* const* argv,const Path& root,const Path& storage,bool development) {
    std::map<std::string,std::string> values;std::set<std::string> flags;
    const std::set<std::string> valued{"--frames","--capture","--steps","--input","--ticks","--output-dir"};
    const std::set<std::string> boolean{"--development","--fresh","--no-audio"};
    if(!development&&argc>1)throw std::runtime_error("Development launch arguments are unavailable in this iOS build");
    for(int index=1;index<argc;++index){
        const std::string key(argv[index]);
        if(values.contains(key)||flags.contains(key))throw std::runtime_error("Duplicate iOS launch argument: "+key);
        if(boolean.contains(key))flags.insert(key);
        else if(valued.contains(key)&&index+1<argc)values[key]=argv[++index];
        else throw std::runtime_error("Unknown or incomplete iOS launch argument: "+key);
    }
    lm::PreviewOptions result;result.executable_directory=root/"runtime";result.storage_directory=storage;
    result.watch_source=false;result.overlay=false;result.player=!flags.contains("--development");
    result.audio=!flags.contains("--no-audio");result.restore_recovery=!flags.contains("--fresh");
    if(values.contains("--frames"))result.frames=integer(values.at("--frames"),1,36000);
    if(values.contains("--capture")){
        if(!result.frames)throw std::runtime_error("iOS --capture requires --frames");
        result.capture=contained(storage,values.at("--capture"));
    }
    if(values.contains("--steps")){
        if(result.frames||values.contains("--capture")||!values.contains("--input")||!values.contains("--ticks")||!values.contains("--output-dir"))
            throw std::runtime_error("iOS timeline capture requires steps, input, ticks and output-dir, with no frame-count capture");
        result.steps=integer(values.at("--steps"),0,36000);result.isolated=true;
        result.timeline=lm::read_json_shared(contained(storage,values.at("--input")),16*1024*1024);
        result.capture_directory=contained(storage,values.at("--output-dir"));
        const auto& ticks=values.at("--ticks");std::size_t start=0;
        while(start<=ticks.size()){
            auto end=ticks.find(',',start);if(end==std::string::npos)end=ticks.size();
            result.capture_ticks.push_back(integer(ticks.substr(start,end-start),0,result.steps));
            if(result.capture_ticks.size()>128)throw std::runtime_error("iOS timeline capture accepts at most 128 ticks");
            if(end==ticks.size())break;start=end+1;
        }
        if(!std::is_sorted(result.capture_ticks.begin(),result.capture_ticks.end())||std::adjacent_find(result.capture_ticks.begin(),result.capture_ticks.end())!=result.capture_ticks.end())
            throw std::runtime_error("iOS capture ticks must be increasing and unique");
    }else if(values.contains("--input")||values.contains("--ticks")||values.contains("--output-dir"))throw std::runtime_error("iOS timeline arguments require --steps");
    if(result.frames||result.steps>=0)result.restore_recovery=false;
    return result;
}
struct InboxCursor { Path after; };
inline void commit_inbox(const Path& storage,const std::function<std::string(const Path&)>& hash,InboxCursor& cursor) {
    const auto inbox=storage/"inbox";
    if(!std::filesystem::exists(inbox))return;
    if(std::filesystem::is_symlink(inbox)||!std::filesystem::is_directory(inbox))throw std::runtime_error("iOS command inbox is not a directory");
    std::vector<Path> ready_files;std::uint64_t count=0;
    for(const auto& entry:std::filesystem::directory_iterator(inbox)){
        const auto id=entry.path().stem().string(),extension=entry.path().extension().string();
        const bool owned=!id.empty()&&id.size()<=96&&id.find_first_not_of("0123456789")==std::string::npos&&(extension==".json"||extension==".ready");
        if(owned&&!entry.is_symlink()&&entry.is_regular_file()&&std::filesystem::file_time_type::clock::now()-entry.last_write_time()>std::chrono::minutes(10)){
            std::filesystem::remove(entry.path());continue;
        }
        if(++count>4096)throw std::runtime_error("iOS inbox exceeds 4096 files; abandoned uploads expire after ten minutes");
        if(entry.path().extension()==".ready"&&!entry.is_symlink()&&entry.is_regular_file())ready_files.push_back(entry.path());
    }
    std::sort(ready_files.begin(),ready_files.end());
    if(ready_files.empty())return;
    const auto found=std::upper_bound(ready_files.begin(),ready_files.end(),cursor.after);
    const std::size_t start=found==ready_files.end()?0:std::size_t(found-ready_files.begin());
    for(std::size_t examined=0;examined<std::min<std::size_t>(64,ready_files.size());++examined){
        const auto& ready=ready_files[(start+examined)%ready_files.size()];
        cursor.after=ready;
        const auto id=ready.stem().string();
        if(id.empty()||id.size()>96||id.find_first_not_of("0123456789")!=std::string::npos||std::filesystem::file_size(ready)>66)continue;
        std::string expected=read_shared_file(ready,66);
        while(!expected.empty()&&(expected.back()=='\r'||expected.back()=='\n'))expected.pop_back();
        if(expected.size()!=64||expected.find_first_not_of("0123456789abcdef")!=std::string::npos)continue;
        const auto input=inbox/(id+".json");
        if(!std::filesystem::exists(input)||std::filesystem::is_symlink(input)||!std::filesystem::is_regular_file(input)||std::filesystem::file_size(input)>16384)continue;
        if(hash(input)!=expected)continue; // An upload still in flight is never parsed or acknowledged.
        const auto command=storage/"commands"/(id+".json");
        if(std::filesystem::exists(command)){
            if(hash(command)!=expected)throw std::runtime_error("Conflicting duplicate iOS command identity");
            std::filesystem::remove(input);
        }else std::filesystem::rename(input,command);
        std::filesystem::remove(ready);
    }
}

}
