#pragma once
#include "planet/shadows.hpp"

namespace planet {
// Branch attachments in tree-local metres for flowers and hanging vines.
struct TreeJoints {
    std::vector<Vec3> floweringTips,vineAnchors;
};
TreeJoints buildTreeBranches(const SurfaceObject& object,Color bark,Color leaves,bool simple,bool fine,
                             std::vector<MeshFace>& faces,std::vector<Triangle>* sprayShadows);
} // namespace planet
