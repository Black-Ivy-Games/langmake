#pragma once
#include "types.hpp"
namespace lm {
Input parse_input(const Json&);
Json input_json(const Input&);
class Timeline {
public:
    explicit Timeline(const Json& document=Json::object());
    Input at(int tick) const;
private:
    struct Event {int from,to; Input input;};
    std::vector<Event> events;
};
}
