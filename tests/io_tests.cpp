#include "langmake/io.hpp"
#include "langmake/timeline.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace lm;
void check(bool result,const char* message){if(!result)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
    auto root=std::filesystem::absolute(argc>1?argv[1]:".")/".cache"/("io-test-"+std::to_string(wall_time_ms()));
    std::filesystem::create_directories(root);auto path=root/"live.json";
    write_json_atomic(path,{{"tick",0}});
    std::atomic<int> errors=0;
    std::string reader_error;
    std::jthread reader([&](std::stop_token token){while(!token.stop_requested()){try{auto j=read_json_shared(path);if(!j.contains("tick"))++errors;}catch(const std::exception&e){++errors;reader_error=e.what();}}});
    for(int i=1;i<100;++i)write_json_atomic(path,{{"tick",i}});
    reader.request_stop();reader.join();if(errors)throw std::runtime_error("Concurrent read: "+reader_error);
#ifdef _WIN32
    HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(lock!=INVALID_HANDLE_VALUE,"Could not acquire contention fixture handle");
    {
        Publications publisher;
        publisher.latest(path,{{"tick",100}});
        for(int i=0;i<200&&publisher.status()["failures"]==0;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(publisher.status()["failures"]>0,"Contention must record failure");
        check(publisher.status()["last_error"].get<std::string>().find("os_error")!=std::string::npos,"Windows error must be retained");
        for(int i=101;i<=200;++i)publisher.latest(path,{{"tick",i}});
        CloseHandle(lock);
        for(int i=0;i<100&&read_json_shared(path)["tick"]!=200;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(read_json_shared(path)["tick"]==200,"Latest observation must recover after lock release");
    }
    auto receipt=root/"blocked-receipt.json";write_json_atomic(receipt,{{"ok",true}});
    lock=CreateFileW(receipt.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(lock!=INVALID_HANDLE_VALUE,"Could not lock receipt");
    {
        Publications publisher;
        check(publisher.receipt(receipt,{{"ok",true},{"id","one"}}),"Receipt enqueue failed");
        for(int i=0;i<200&&publisher.status()["failures"]==0;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        publisher.latest(path,{{"tick",201}});
        for(int i=0;i<100&&read_json_shared(path)["tick"]!=201;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(read_json_shared(path)["tick"]==201,"Locked receipt must not starve live observations");
        check(!publisher.receipt(root/"large.json",{{"unbounded",std::string(9*1024*1024,'x')}}),"Oversized receipt must be rejected");
        check(publisher.status()["queued_receipt_bytes"].get<size_t>()<=16*1024*1024,"Receipt memory must be bounded");
        CloseHandle(lock);
    }
#endif
    bool duplicate=false;try{parse_json_strict("{\"a\":1,\"a\":2}");}catch(...){duplicate=true;}check(duplicate,"Duplicate JSON keys must fail");
    Timeline t(Json{{"events",Json::array({{{"from",0},{"to",2},{"move_x",1}},{{"from",1},{"to",3},{"actions",{{"interact",true}}}}})}});
    check(t.at(0).move_x==1,"Timeline start");check(t.at(1).move_x==0&&t.at(1).actions.at("interact")==1,"Last active event replaces input");check(t.at(3).actions.empty(),"Exclusive timeline end");
    auto filtered=filter_observation({{"entities",Json::array({{{"id","b"},{"role","enemy"},{"position",{1,2,3}}},{{"id","a"},{"role","enemy"},{"position",{4,5,6}}}})}},{{"role","enemy"},{"limit",1},{"fields",{"position"}}});
    check(filtered["matched_count"]==2&&filtered["entities"][0]["id"]=="a"&&filtered["next_offset"]==1,"Stable filtered pagination");
    std::cout<<"IO/timeline checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
