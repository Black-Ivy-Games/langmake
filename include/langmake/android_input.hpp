#pragma once
#include "types.hpp"
#include <SDL3/SDL_rect.h>
#include <memory>
union SDL_Event;
namespace lm {
class UiRenderer;
// Touch routing is platform-independent so its ownership/cancellation rules can
// be tested on desktop. Rendering uses the same text-authored UI renderer.
class MobileControls {
public:
    MobileControls(const Json& input, const std::filesystem::path& runtime, bool headless = false);
    ~MobileControls();
    void configure(const Json& input);
    void resize(int width, int height, float density, SDL_Rect safe_area, UiRenderer* game_ui = nullptr);
    bool process_event(const SDL_Event& event, UiRenderer& game_ui);
    void apply(Input& input) const;
    void consume_tick();
    void reset(UiRenderer* game_ui = nullptr);
    void draw(double time);
    Json inspect() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
