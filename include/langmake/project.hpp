#pragma once
#include "types.hpp"

namespace lm {
Project load_project(const std::filesystem::path& path, const std::string& profile = "");
Json validate_document(const Json& document);
Json project_summary(const Project& project);
Json capabilities();
Json project_schema();
Json patch_project(const std::filesystem::path& path, const Json& patch, const std::string& profile = "");
Json checkpoint_project(const std::filesystem::path& path, const std::string& name);
Json restore_project(const std::filesystem::path& path, const std::string& name);
// Copy only referenced authored sources into a new directory. Reject stale
// Project revisions and existing destinations; rename staged content on success.
void copy_project_sources(const Project& project, const std::filesystem::path& destination);
// Compile and replace one registered recipe, using the full project revision.
Json replace_asset(const std::filesystem::path& path, const std::string& id,
                   const std::string& expected_revision, const Json& document, const std::string& profile = "");
std::string save_structure_fingerprint(const Project& project);
Json save_compatibility_report(const Project& project, const Json& saved);
std::string revision_for(const std::string& bytes);
}
