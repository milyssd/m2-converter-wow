// wxl-equip-extension: client-side virtual M2 path table.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wxl::scripts::equipextension
{
    /**
     * @brief Builds a normalized virtual .m2 key into out[outSz].
     *
     * Geosets are sorted and deduplicated internally. The full texture path is
     * included in the cache identity, so equal basenames in different folders
     * do not share a filtered model. Invalid inputs and truncation return 0.
     * @param out         destination buffer
     * @param outSz       destination buffer size in bytes
     * @param cmo         CharModelObject pointer, used as the character identifier
     * @param realMdxPath real archive path of the collection .mdx
     * @param geoIds      geoset IDs included in the filter (unsorted)
     * @param geoCount    number of IDs
     * @param texPath     full BLP path for the texture slot (may be empty)
     * @param relocateEmbeddedTextures redirect hardcoded M2 textures to the model directory
     * @param attachmentId attachment point to isolate render instances; UINT32_MAX omits it
     * @return number of characters written (excluding null), or 0 on invalid input/truncation
     */
    size_t VPathBuildKey(char* out, size_t outSz, void* cmo,
                         const char* realMdxPath,
                         const uint16_t* geoIds, uint32_t geoCount,
                         const char* texPath, bool relocateEmbeddedTextures = false,
                         uint32_t attachmentId = UINT32_MAX);

    /**
     * @brief Ensures virtual .m2 and available .skin bytes are in the client serve table.
     *
     * Reads the real .m2 and its declared skin profiles from the archive on first call;
     * subsequent calls with the same key are no-ops. Registers all paths under cmo for
     * cleanup via VPathEvictCmo.
     * @param cmo         CharModelObject pointer
     * @param realMdxPath real archive path of the collection .mdx
     * @param geoIds      geoset IDs (unsorted)
     * @param geoCount    number of IDs
     * @param texPath     full BLP path for the texture slot (may be empty)
     * @param relocateEmbeddedTextures redirect hardcoded M2 textures to the model directory
     * @param attachmentId attachment point to isolate render instances; UINT32_MAX omits it
     * @return true when the model is available, false on invalid paths/files
     */
    bool VPathPopulate(void* cmo, const char* realMdxPath,
                       const uint16_t* geoIds, uint32_t geoCount,
                       const char* texPath, bool relocateEmbeddedTextures = false,
                       uint32_t attachmentId = UINT32_MAX);

    /**
     * @brief Removes all virtual table entries owned by cmo.
     *
     * Call when the cmo's sceneNode goes null (character evicted from the scene).
     * @param cmo  CharModelObject pointer to evict
     */
    void VPathEvictCmo(void* cmo);
}
