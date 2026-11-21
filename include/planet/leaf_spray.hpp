#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace planet {
enum class LeafSpray { Broad, Narrow, Needles };
struct LeafUV {
    float u=.75f,v=.75f; // White, opaque tile for bark and other materials.
    bool cutout() const {return u<.5f || v<.5f;}
};
constexpr int leafTileSize=128,leafAtlasSize=2*leafTileSize;
LeafUV leafCoordinates(LeafSpray kind,float u,float v);
// Three cutout shoots with a stem at (0.5,0), plus a solid tile.
// Pixels are RGBA8, rows run from the attachment to the tip.
using LeafPixel=std::array<std::uint8_t,4>;
std::vector<LeafPixel> makeLeafAtlas();
} // namespace planet
