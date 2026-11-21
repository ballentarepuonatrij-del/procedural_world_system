#pragma once
#include "planet/shadows.hpp"

namespace planet {
// Attachments in plant-local metres for flowers and hanging vines.
struct BranchJoints {
    std::vector<Vec3> floweringTips,vineAnchors;
};
BranchJoints buildBranches(const SurfaceObject& object,Color bark,Color leaves,bool simple,bool fine,
                           std::vector<MeshFace>& faces,std::vector<Triangle>* sprayShadows);
} // namespace planet
