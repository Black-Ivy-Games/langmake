#include "langmake/io.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
namespace lm {
namespace fs = std::filesystem;
std::int64_t wall_time_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
namespace {
[[noreturn]] void file_error(const char* operation, const fs::path& path, unsigned long code) {
    throw std::runtime_error(Json({{"code","file_io"},{"operation",operation},{"path",path.generic_string()},
        {"os_error",code},{"message",std::string(operation)+" failed: "+path.generic_string()+" (OS error "+std::to_string(code)+")"}}).dump());
}
}
std::string read_shared_file(const fs::path& path, std::size_t maximum) {
#ifdef _WIN32
    HANDLE h=INVALID_HANDLE_VALUE;
    constexpr int delays[]{0,1,2,5,10,20,40,80};
    for(int i=0;i<8;++i){
        if(delays[i])std::this_thread::sleep_for(std::chrono::milliseconds(delays[i]));
        h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h!=INVALID_HANDLE_VALUE)break;
        auto error=GetLastError();if(i==7||(error!=ERROR_ACCESS_DENIED&&error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION&&error!=ERROR_FILE_NOT_FOUND))file_error("read/open",path,error);
    }
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(h,&size)) { auto e=GetLastError(); CloseHandle(h); file_error("read/size",path,e); }
    if(size.QuadPart<0 || static_cast<unsigned long long>(size.QuadPart)>maximum) { CloseHandle(h); throw std::runtime_error("File exceeds bounded read limit: "+path.string()); }
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0'); DWORD got=0;
    bool ok=ReadFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)!=0;
    auto e=GetLastError(); CloseHandle(h);
    if(!ok || got!=bytes.size()) file_error("read/copy",path,ok ? ERROR_HANDLE_EOF : e);
    return bytes;
#else
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in) throw std::runtime_error("Cannot open "+path.string());
    auto size=in.tellg(); if(size<0 || static_cast<size_t>(size)>maximum) throw std::runtime_error("File exceeds bounded read limit");
    std::string bytes(static_cast<size_t>(size),'\0'); in.seekg(0); in.read(bytes.data(),size); if(!in) throw std::runtime_error("Incomplete read"); in.close(); return bytes;
#endif
}
Json parse_json_strict(const std::string& bytes) {
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>128) throw std::runtime_error("JSON nesting exceeds 128 levels");
        if(event==Json::parse_event_t::object_start) keys.emplace_back();
        if(event==Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) throw std::runtime_error("Duplicate JSON object key: "+value.get<std::string>());
        if(event==Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
Json read_json_shared(const fs::path& path,std::size_t maximum) { return parse_json_strict(read_shared_file(path,maximum)); }
void write_json_atomic(const fs::path& path,const Json& value) {
    static std::atomic<uint64_t> serial{0};
    static const auto nonce=std::random_device{}();
    if(!path.parent_path().empty()) fs::create_directories(path.parent_path());
    auto temp=path; temp+="."+std::to_string(wall_time_ms())+"-"+std::to_string(nonce)+"-"+std::to_string(serial++)+".tmp";
    try {
        { std::ofstream out(temp,std::ios::binary); auto bytes=value.dump(); out.write(bytes.data(),bytes.size()); out.close(); if(!out) throw std::runtime_error("Incomplete write: "+temp.string()); }
#ifdef _WIN32
        constexpr int delays[]{0,2,5,10,20,40,80};
        for(int i=0;i<7;++i) {
            if(delays[i]) std::this_thread::sleep_for(std::chrono::milliseconds(delays[i]));
            // ReplaceFile keeps the existing destination's identity/ACL handling
            // coherent while readers retain delete-sharing handles.
            if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) {
                if(ReplaceFileW(path.c_str(),temp.c_str(),nullptr,REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr))return;
                auto code=GetLastError();
                if(code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND && code!=ERROR_UNABLE_TO_REMOVE_REPLACED && code!=ERROR_UNABLE_TO_MOVE_REPLACEMENT && code!=ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 && code!=ERROR_SHARING_VIOLATION && code!=ERROR_LOCK_VIOLATION && code!=ERROR_ACCESS_DENIED)file_error("replace",path,code);
            }
            if(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return;
            auto code=GetLastError();
            if(i==6 || (code!=ERROR_SHARING_VIOLATION && code!=ERROR_LOCK_VIOLATION && code!=ERROR_ACCESS_DENIED)) file_error("replace",path,code);
        }
#else
        fs::rename(temp,path);
#endif
    } catch(...) { std::error_code ignored; fs::remove(temp,ignored); throw; }
}
struct Publications::Impl {
    struct Item { fs::path path; Json value; std::int64_t expires; std::size_t bytes=0; };
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::deque<Item> receipts;
    std::unique_ptr<Item> latest;
    bool stop=false;
    bool receipt_turn=false;
    std::size_t receipt_bytes=0;
    std::int64_t shutdown_deadline=0;
    uint64_t failures=0,published=0;
    std::int64_t last_success=0;
    std::string last_error;
    std::thread worker;
    Impl():worker([this]{run();}){}
    ~Impl(){ {std::lock_guard guard(mutex);stop=true;shutdown_deadline=wall_time_ms()+500;} cv.notify_one(); worker.join(); }
    void run() {
        for(;;) {
            Item item; bool live=false,stopping=false;
            { std::unique_lock lock(mutex); cv.wait(lock,[&]{return stop||latest||!receipts.empty();});
              stopping=stop;
              if(stopping&&wall_time_ms()>shutdown_deadline)return;
              if(latest&&(!receipt_turn||receipts.empty())) {item=std::move(*latest);latest.reset();live=true;receipt_turn=true;}
              else if(!receipts.empty()) {item=std::move(receipts.front());receipts.pop_front();receipt_turn=false;}
              else return;
            }
            try { write_json_atomic(item.path,item.value); std::lock_guard guard(mutex); ++published; if(live)last_success=wall_time_ms();else receipt_bytes-=item.bytes; }
            catch(const std::exception& e) {
                { std::lock_guard guard(mutex); ++failures;last_error=e.what();
                  if(!stopping && wall_time_ms()<item.expires) { if(live) {if(!latest) latest=std::make_unique<Item>(std::move(item));} else receipts.push_back(std::move(item)); }
                  else if(!live)receipt_bytes-=item.bytes;
                }
                if(!stopping) std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            if(stopping) { std::lock_guard guard(mutex); if(receipts.empty()&&!latest)return; }
        }
    }
};
Publications::Publications():impl(std::make_unique<Impl>()){}
Publications::~Publications()=default;
void Publications::latest(const fs::path& path,Json value) {std::lock_guard guard(impl->mutex);impl->latest=std::make_unique<Impl::Item>(Impl::Item{path,std::move(value),wall_time_ms()+3000});impl->cv.notify_one();}
bool Publications::receipt(const fs::path& path,Json value) {
    auto bytes=value.dump().size();
    if(bytes>8*1024*1024&&value.contains("state")){value.erase("state");value["state_omitted"]="Response exceeds 8 MiB; query fewer entities or fields. Command outcome is unchanged.";bytes=value.dump().size();}
    std::lock_guard guard(impl->mutex);
    if(bytes>8*1024*1024||impl->receipts.size()>=1024||impl->receipt_bytes+bytes>16*1024*1024)return false;
    impl->receipt_bytes+=bytes;impl->receipts.push_back({path,std::move(value),wall_time_ms()+600000,bytes});impl->cv.notify_one();return true;
}
bool Publications::available() const {std::lock_guard guard(impl->mutex);return impl->receipts.size()<1000&&impl->receipt_bytes<=8*1024*1024;}
Json Publications::status() const {std::lock_guard guard(impl->mutex);return {{"failures",impl->failures},{"writes",impl->published},{"last_live_success_ms",impl->last_success},{"last_error",impl->last_error},{"queued_receipts",impl->receipts.size()},{"queued_receipt_bytes",impl->receipt_bytes},{"receipt_capacity_bytes",16*1024*1024}};}
Json filter_observation(Json state,const Json& query) {
    if(!state.contains("entities")||!state["entities"].is_array())return state;
    auto items=std::move(state["entities"]); Json matched=Json::array();
    for(auto& entity:items) {
        if(query.contains("ids") && std::find(query["ids"].begin(),query["ids"].end(),entity.value("id",""))==query["ids"].end()) continue;
        if(query.contains("role") && entity.value("role","")!=query["role"].get<std::string>())continue;
        if(query.contains("visible") && entity.value("visible",true)!=query["visible"].get<bool>())continue;
        matched.push_back(std::move(entity));
    }
    std::sort(matched.begin(),matched.end(),[](const Json&a,const Json&b){return a.value("id","")<b.value("id","");});
    int offset=query.value("offset",0),limit=query.value("limit",128);
    if(offset<0||limit<0||limit>4096)throw std::runtime_error("Observation offset must be nonnegative and limit 0..4096");
    state["entity_count"]=items.size();state["matched_count"]=matched.size();state["entities"]=Json::array();
    for(size_t i=static_cast<size_t>(offset);i<matched.size()&&i<static_cast<size_t>(offset+limit);++i) {
        if(query.contains("fields")){Json selected=Json::object();selected["id"]=matched[i].at("id");for(const auto& f:query["fields"])if(matched[i].contains(f.get<std::string>()))selected[f.get<std::string>()]=matched[i][f.get<std::string>()];state["entities"].push_back(std::move(selected));}
        else state["entities"].push_back(std::move(matched[i]));
    }
    state["returned_count"]=state["entities"].size();state["offset"]=offset;
    state["next_offset"]=offset+state["entities"].size()<matched.size()?Json(offset+state["entities"].size()):Json(nullptr);
    return state;
}
}
