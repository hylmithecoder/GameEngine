#pragma once
#include "AnimationAuthoring.hpp"

namespace ilmeee {

// Project-relative, canonical containment check, including symlink ancestors.
// Writers may create a missing leaf; readers require an existing regular file.
bool ResolveAnimationAsset(const std::string &project, const std::string &path,
                           bool writing, std::string &absolute,
                           std::string &relative, std::string &error);

// Renderer-free implementation shared by the native editor MCP and tests.
// index is interpreted by the caller; bones is the selected object's rig.
// Writes commit only after the entire request validates. Scene assignment is
// separate (set_animation); file edits do not enter scene Undo/Redo history.
nlohmann::json HandleAnimationTool(const std::string &tool,
                                   const nlohmann::json &args,
                                   const std::string &project,
                                   const std::vector<SkeletonBone> &bones);

} // namespace ilmeee
