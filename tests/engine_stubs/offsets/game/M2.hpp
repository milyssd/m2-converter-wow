#pragma once
#include <cstddef>

// Deliberately portable layouts, not the client's x86 binary layouts.
namespace wxl::offsets::game::m2 {
constexpr size_t kOffCmoSceneNode = 0;
constexpr size_t kOffCmoRace = 8;
constexpr size_t kOffCmoGender = 12;
constexpr size_t kOffSceneNodeOwner = 0;
constexpr size_t kOffInstModel = 8;
constexpr size_t kOffInstInitFlags = 16;
constexpr size_t kOffInstBonePalette = 24;
constexpr size_t kOffModelHeader = 0;
constexpr size_t kOffModelSkin = 8;
constexpr size_t kOffModelPathStem = 16;
constexpr size_t kOffHdrBoneCount = 0;
constexpr size_t kOffHdrBoneArray = 8;
constexpr size_t kOffHdrBoneIdxLutCount = 16;
constexpr size_t kOffHdrBoneIdxLutPtr = 24;
constexpr size_t kBoneStride = 16;
constexpr size_t kOffBoneNameCrc = 0;
constexpr size_t kOffBoneKeyId = 4;
constexpr size_t kOffBoneParent = 8;
constexpr size_t kBonePaletteStride = 48;
}
