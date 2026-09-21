#pragma once
#include "types.hpp"

namespace lm {
// Validates the complete graph and produces a new, self-contained player
// content directory. Does not include authoring caches, saves or credentials.
Json cook_project(const std::filesystem::path& project,
                  const std::filesystem::path& output,
                  const std::string& target, const std::string& profile = "");
}
