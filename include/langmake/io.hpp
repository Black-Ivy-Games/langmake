#pragma once
#include "types.hpp"
#include <memory>
namespace lm {
std::int64_t wall_time_ms();
std::string read_shared_file(const std::filesystem::path&, std::size_t maximum = 4*1024*1024);
Json parse_json_strict(const std::string&);
Json read_json_shared(const std::filesystem::path&, std::size_t maximum = 4*1024*1024);
void write_json_atomic(const std::filesystem::path&, const Json&);
// Coalesced telemetry and durable-in-session receipts are published off the frame thread.
class Publications {
public:
    Publications();
    ~Publications();
    void latest(const std::filesystem::path&, Json);
    bool receipt(const std::filesystem::path&, Json);
    bool available() const;
    Json status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
Json filter_observation(Json state, const Json& query);
}
