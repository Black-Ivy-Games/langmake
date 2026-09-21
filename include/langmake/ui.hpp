#pragma once
#include "types.hpp"
#include <memory>
union SDL_Event;
namespace lm {
class UiRenderer {
public:
    enum class PointerPhase {down, move, up, cancel};
    // Headless mode validates/layouts the same documents without creating GPU
    // resources. It is intended for native UI tests and semantic inspection.
    UiRenderer(const Project& project, const std::filesystem::path& executable_directory, bool headless = false);
    ~UiRenderer();
    bool process_event(const SDL_Event& event);
    bool captures_keyboard() const;
    bool consumes_key(int sdl_scancode) const;
    bool consumes_gamepad_button(int sdl_button) const;
    // Action-bound pointers use physical framebuffer coordinates and independent
    // IDs; a pointer never changes its owning control until release/cancel.
    bool process_pointer(std::int64_t id, PointerPhase phase, float x, float y);
    void apply(Input& input) const;
    void consume_tick();
    void reset_input();
    // Viewport dimensions are physical pixels. Density maps authored dp units
    // to physical pixels; SDL input coordinates retain their window pixel ratio.
    void update(const Json& state, double time, int width, int height, float density = 1);
    // Optional physical-pixel content rectangle. Layout/input are inset while
    // captures and rendering retain the complete framebuffer. Zero clears it.
    void set_safe_area(std::array<int,4> rectangle);
    void draw();
    Json drain_events();
    Json inspect() const;
    Json diagnostics(const std::string& expected_characters = "") const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
