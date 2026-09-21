#pragma once
#include "io.hpp"
#include <filesystem>

namespace lm {
// Preserve rejected bytes, including invalid JSON, before a fresh game may
// replace its automatic save. Only one previous rejected slot is retained.
inline Json quarantine_mobile_recovery(const std::filesystem::path& active,const std::string& diagnostic) {
    const auto rejected=active.parent_path()/(active.stem().string()+".rejected.json");
    auto reason=rejected;reason+=".reason.json";
    Json result{{"preserved",false},{"path",rejected.string()},{"diagnostic",diagnostic.substr(0,4096)}};
    try {
        if(!std::filesystem::is_regular_file(active)||std::filesystem::is_symlink(active))throw std::runtime_error("Automatic recovery source is not a regular file");
        if(std::filesystem::exists(rejected)) {
            if(!std::filesystem::is_regular_file(rejected)||std::filesystem::is_symlink(rejected))throw std::runtime_error("Rejected recovery destination is not a regular file");
            std::filesystem::remove(rejected);
        }
        std::filesystem::rename(active,rejected);
        result["preserved"]=true;
        write_json_atomic(reason,result);
    }catch(const std::exception& error){result["preserved"]=false;result["preservation_error"]=error.what();}
    return result;
}
inline Json quarantine_android_recovery(const std::filesystem::path& active,const std::string& diagnostic) {
    return quarantine_mobile_recovery(active,diagnostic);
}
}
