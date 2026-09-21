#include "langmake/ios_contract.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Path=std::filesystem::path;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<typename Work>void rejects(Work work,const char* message){bool failed=false;try{work();}catch(const std::exception&){failed=true;}check(failed,message);}
void write(const Path& path,const std::string& text){std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream<<text;check(bool(stream),"Fixture write failed");}
std::string read(const Path& path){std::ifstream stream(path,std::ios::binary);return {(std::istreambuf_iterator<char>(stream)),{}};}
lm::PreviewOptions parse(const Path& root,const Path& storage,std::initializer_list<const char*> args,bool development=true){
    std::vector<const char*> argv{"langmake"};argv.insert(argv.end(),args.begin(),args.end());
    return lm::ios::parse_options(int(argv.size()),argv.data(),root,storage,development);
}
}
int main(int argc,char** argv){try{
    const auto root=std::filesystem::absolute(argc>1?argv[1]:".")/".cache"/("ios-contract-"+std::to_string(lm::wall_time_ms()));
    const auto storage=root/"state";std::filesystem::create_directories(storage/"commands");std::filesystem::create_directories(storage/"inbox");
    const auto release=parse(root,storage,{},false);
    check(release.player&&!release.overlay&&!release.watch_source&&release.restore_recovery,"Release defaults must be player-only with recovery");
    rejects([&]{parse(root,storage,{"--fresh"},false);},"Release accepted development arguments");
    const auto development=parse(root,storage,{"--development","--fresh","--no-audio"});
    check(!development.player&&!development.restore_recovery&&!development.audio,"Development launch flags were not applied");
    rejects([&]{parse(root,storage,{"--frames","0"});},"Zero frame-count capture accepted");
    rejects([&]{parse(root,storage,{"--frames","36001"});},"Unbounded frame-count capture accepted");
    rejects([&]{parse(root,storage,{"--frames","1bad"});},"Invalid numeric suffix accepted");
    rejects([&]{parse(root,storage,{"--fresh","--fresh"});},"Duplicate boolean launch argument accepted");
    rejects([&]{parse(root,storage,{"--frames","1","--frames","2"});},"Duplicate valued launch argument accepted");
    rejects([&]{parse(root,storage,{"--engine-code","arbitrary"});},"Unknown launch argument accepted");
    rejects([&]{parse(root,storage,{"--capture","capture.png"});},"Capture without frame count accepted");
    const auto frames=parse(root,storage,{"--frames","2","--capture","capture.png"});
    check(frames.frames==2&&frames.capture==storage/"capture.png"&&!frames.restore_recovery,"Frame capture options differ");
    rejects([&]{parse(root,storage,{"--frames","2","--capture","../escape.png"});},"Capture path traversal accepted");
    write(storage/"input.json","{}");
    const auto timeline=parse(root,storage,{"--steps","60","--input","input.json","--ticks","0,30,60","--output-dir","captures"});
    check(timeline.steps==60&&timeline.isolated&&!timeline.restore_recovery&&timeline.capture_ticks==std::vector<int>{0,30,60},"Timeline tick boundaries differ");
    rejects([&]{parse(root,storage,{"--steps","60","--input","input.json","--ticks","30,0","--output-dir","captures"});},"Unsorted capture ticks accepted");
    rejects([&]{parse(root,storage,{"--steps","60","--input","input.json","--ticks","0,0","--output-dir","captures"});},"Duplicate capture ticks accepted");
    rejects([&]{parse(root,storage,{"--steps","60","--input","input.json","--ticks","61","--output-dir","captures"});},"Capture tick beyond simulation accepted");
    rejects([&]{parse(root,storage,{"--steps","60","--input","input.json","--ticks","0","--output-dir","captures","--frames","1"});},"Mixed timing modes accepted");
    rejects([&]{parse(root,storage,{"--ticks","0"});},"Orphan timeline argument accepted");
    rejects([&]{lm::ios::contained(storage,"../outside");},"Relative storage escape accepted");
    rejects([&]{lm::ios::contained(storage,(root/"outside").string());},"Absolute storage escape accepted");
    rejects([&]{lm::ios::contained(storage,".");},"Storage directory accepted as output file");
    std::filesystem::create_directory(root/"outside");std::error_code symbolic_error;
    std::filesystem::create_directory_symlink(root/"outside",storage/"escape-link",symbolic_error);
    if(!symbolic_error)rejects([&]{lm::ios::contained(storage,"escape-link/data.json");},"Symlink storage escape accepted");
    else std::cout<<"SKIP symlink creation unavailable on host: "<<symbolic_error.message()<<'\n';

    // Use a deterministic injected digest to exercise the actual native commit
    // algorithm without substituting the iOS CommonCrypto SHA implementation.
    const auto hash=[](const Path& path){return std::string(64,read(path)=="complete"?'a':'b');};
    lm::ios::InboxCursor cursor;
    write(storage/"inbox/100.json","partial");write(storage/"inbox/100.ready",std::string(64,'a')+"\n");
    lm::ios::commit_inbox(storage,hash,cursor);
    check(!std::filesystem::exists(storage/"commands/100.json")&&std::filesystem::exists(storage/"inbox/100.ready"),"Incomplete upload was committed");
    write(storage/"inbox/100.json","complete");lm::ios::commit_inbox(storage,hash,cursor);
    check(read(storage/"commands/100.json")=="complete"&&!std::filesystem::exists(storage/"inbox/100.ready")&&!std::filesystem::exists(storage/"inbox/100.json"),"Complete upload did not commit atomically");
    write(storage/"inbox/100.json","complete");write(storage/"inbox/100.ready",std::string(64,'a'));
    lm::ios::commit_inbox(storage,hash,cursor);check(!std::filesystem::exists(storage/"inbox/100.ready"),"Duplicate identical upload did not retire");
    write(storage/"inbox/101.json",std::string(16385,'x'));write(storage/"inbox/101.ready",std::string(64,'b'));
    lm::ios::commit_inbox(storage,hash,cursor);check(!std::filesystem::exists(storage/"commands/101.json"),"Oversized request committed");
    write(storage/"inbox/102.json","complete");write(storage/"inbox/102.ready","invalid");
    lm::ios::commit_inbox(storage,hash,cursor);check(!std::filesystem::exists(storage/"commands/102.json"),"Malformed ready marker committed");
    write(storage/"inbox/103.json","complete");write(storage/"inbox/103.ready",std::string(64,'a'));write(storage/"commands/103.json","conflict");
    rejects([&]{lm::ios::commit_inbox(storage,hash,cursor);},"Conflicting duplicate command overwritten");
    check(read(storage/"commands/103.json")=="conflict","Conflicting command was mutated");
    const auto liveness=root/"liveness";std::filesystem::create_directories(liveness/"commands");
    for(int id=1000;id<1064;++id)write(liveness/"inbox"/(std::to_string(id)+".ready"),"incomplete");
    write(liveness/"inbox/9999.json","complete");write(liveness/"inbox/9999.ready",std::string(64,'a'));
    lm::ios::InboxCursor rotating;
    lm::ios::commit_inbox(liveness,hash,rotating);
    check(!std::filesystem::exists(liveness/"commands/9999.json"),"Inbox exceeded its 64-marker frame budget");
    lm::ios::commit_inbox(liveness,hash,rotating);
    check(std::filesystem::exists(liveness/"commands/9999.json"),"Incomplete uploads starved a later valid command");
    write(liveness/"inbox/5000.json","abandoned");write(liveness/"inbox/5000.ready","incomplete");write(liveness/"evidence/proof.json","preserve");
    const auto old=std::filesystem::file_time_type::clock::now()-std::chrono::minutes(11);
    std::filesystem::last_write_time(liveness/"inbox/5000.json",old);std::filesystem::last_write_time(liveness/"inbox/5000.ready",old);
    lm::ios::commit_inbox(liveness,hash,rotating);
    check(!std::filesystem::exists(liveness/"inbox/5000.json")&&!std::filesystem::exists(liveness/"inbox/5000.ready"),"Abandoned upload pair did not expire");
    check(read(liveness/"evidence/proof.json")=="preserve","Inbox maintenance altered requested evidence");
    std::cout<<"PASS iOS release gate, bounded launch arguments, timing modes, storage containment, atomic inbox commit, queue fairness and abandoned-upload retention\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
