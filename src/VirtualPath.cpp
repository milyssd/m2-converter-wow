// wxl-equip-extension: client-side virtual M2 path table and file provider.
//
// Virtual paths encode (cmo × model × merged-geoset-filter × texture) in the filename so the
// engine's model hash table sees a distinct cache key for every unique combination. The host
// serve hook is bypassed; bytes are served directly from an in-process table. Filtering is still
// applied by OnM2SkinFinalize on the parsed rawTri. Optional texture relocation rewrites
// only hardcoded texture filename descriptors, preserving the original geometry.
//
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

#include "VirtualPath.hpp"

#include "Settings.hpp"
#include "runtime/storage/StorageHook.hpp"
#include "game/io/Io.hpp"
#include "offsets/engine/Io.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl::scripts::equipextension
{
    namespace
    {
        // Same physical log file and toggle as EquipExtension.cpp's EquipLog -- a second
        // BoolSetting instance pointing at the same [EquipExtension] Log key is safe (each
        // instance only reads/writes its own key; whichever constructs first wins the write).
        settings::BoolSetting g_logSetting(
            "EquipExtension", "Log",
            "Write diagnostic output to WarcraftXL_equip.log. Disabled by default -- the log "
            "grows to 100+ MB during normal play. Set to 1 only when diagnosing bone/attach issues.",
            false);

        static void VPathLog(const char* fmt, ...) noexcept
        {
            if (!g_logSetting.Value()) return;
#pragma warning(suppress: 4996)
            FILE* f = std::fopen("WarcraftXL_equip.log", "a");
            if (!f) return;
            va_list ap; va_start(ap, fmt);
            std::vfprintf(f, fmt, ap);
            va_end(ap);
            std::fputc('\n', f);
            std::fclose(f);
        }
        // Storage providers also run on loader threads. Protect table publication,
        // copies and eviction, without holding this mutex during reentrant game I/O.
        std::mutex g_virtualMutex;
        // virtual path -> file bytes (.m2 and its declared .skin profiles).
        std::unordered_map<std::string, std::vector<uint8_t>> g_virtualBytes;

        // cmo -> list of virtual paths it owns, for O(n) cleanup on eviction.
        std::unordered_map<void*, std::vector<std::string>> g_cmoVPaths;

        // ─── Key building ─────────────────────────────────────────────────────

        static bool SortIds(uint16_t* sorted, uint32_t& n,
                            const uint16_t* ids, uint32_t count) noexcept
        {
            if (count > 16 || (count && !ids)) return false;
            for (uint32_t i = 0; i < count; ++i) sorted[i] = ids[i];
            std::sort(sorted, sorted + count);
            n = static_cast<uint32_t>(std::unique(sorted, sorted + count) - sorted);
            return true;
        }

        static char LowerAscii(char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }

        static std::string NormalizePath(const char* path)
        {
            std::string result = path ? path : "";
            for (char& c : result) c = (c == '/') ? '\\' : LowerAscii(c);
            return result;
        }

        // A dot in a directory name is not a file extension.
        static size_t ExtensionOffset(const std::string& path) noexcept
        {
            size_t dot = path.find_last_of('.');
            size_t slash = path.find_last_of('\\');
            return dot != std::string::npos &&
                   (slash == std::string::npos || dot > slash) ? dot : path.size();
        }

        static std::string Hex(uintptr_t value)
        {
            char buffer[2 * sizeof(uintptr_t) + 1];
            size_t len = 0;
            do { buffer[len++] = "0123456789abcdef"[value & 15]; value >>= 4; } while (value);
            std::reverse(buffer, buffer + len);
            return std::string(buffer, len);
        }

        // Stable hash of the entire normalized texture path. The original key used
        // only its basename, colliding for different folders with equal filenames.
        static std::string TextureHash(const std::string& path)
        {
            uint64_t hash = UINT64_C(14695981039346656037);
            for (unsigned char c : path) { hash ^= c; hash *= UINT64_C(1099511628211); }
            char buffer[17];
            for (int i = 15; i >= 0; --i) { buffer[i] = "0123456789abcdef"[hash & 15]; hash >>= 4; }
            return std::string(buffer, 16);
        }

        // Builds the virtual .m2 key (sortedIds must already be sorted ascending).
        // Format: <stem>_wxl_<id0>_<id1>..._tex_<texbasename>_cmo<hex>.m2  (all lowercase)
        // The engine normalises all paths to lowercase and uses .m2; keys must match that form.
        static size_t BuildKey(char* out, size_t outSz, void* cmo,
                                const char* realMdxPath,
                                const uint16_t* sortedIds, uint32_t idCount,
                                const char* texPath, bool relocateEmbeddedTextures,
                                uint32_t attachmentId)
        {
            if (!out || outSz == 0) return 0;
            out[0] = '\0';
            if (!realMdxPath || !*realMdxPath) return 0;
            std::string path = NormalizePath(realMdxPath);
            std::string key = path.substr(0, ExtensionOffset(path)) + "_wxl_";
            for (uint32_t i = 0; i < idCount; ++i)
            {
                if (i) key += '_';
                key += std::to_string(sortedIds[i]);
            }
            if (texPath && *texPath)
            {
                std::string texture = NormalizePath(texPath);
                size_t slash = texture.find_last_of('\\');
                size_t start = slash == std::string::npos ? 0 : slash + 1;
                key += "_tex_" + texture.substr(start, ExtensionOffset(texture) - start);
                key += "_p" + TextureHash(texture);
            }
            if (relocateEmbeddedTextures) key += "_rt1";
            if (attachmentId != UINT32_MAX) key += "_ap" + std::to_string(attachmentId);
            key += "_cmo" + Hex(reinterpret_cast<uintptr_t>(cmo)) + ".m2";
            if (key.size() >= outSz) return 0;
            std::memcpy(out, key.c_str(), key.size() + 1);
            return key.size();
        }

        // ─── File reading ─────────────────────────────────────────────────────

        // Reads all bytes of a game archive file via the existing IO wrappers.
        // Uses kOpenWholeFile so the handle buffer holds the full content immediately.
        static bool ReadGameFile(const char* path, std::vector<uint8_t>& out) noexcept
        {
            namespace io    = wxl::game::io;
            namespace iooff = wxl::offsets::engine::io;

            out.clear();
            void* handle = nullptr;
            if (!io::FileOpen(path, iooff::kOpenWholeFile, &handle) || !handle)
                return false;

            uint32_t sizeHigh = 0;
            uint32_t size     = io::FileSize(handle, &sizeHigh);
            bool ok = false;
            if (size > 0 && sizeHigh == 0)
            {
                out.resize(size);
                uint32_t got = 0;
                io::FileRead(handle, out.data(), size, &got);
                ok = (got == size);
                if (!ok) out.clear();
            }
            io::FileClose(handle);
            return ok;
        }

        // All .skin paths must fit as complete names. Never serve truncated aliases.
        static bool SkinPath(char* out, size_t outSz, const char* modelPath, uint32_t index)
        {
            if (!out || !outSz || !modelPath || index > 99) return false;
            out[0] = '\0';
            std::string path = NormalizePath(modelPath);
            path.resize(ExtensionOffset(path));
            path += static_cast<char>('0' + index / 10);
            path += static_cast<char>('0' + index % 10);
            path += ".skin";
            if (path.size() >= outSz) return false;
            std::memcpy(out, path.c_str(), path.size() + 1);
            return true;
        }

        static uint32_t ReadU32(const std::vector<uint8_t>& bytes, size_t offset) noexcept
        {
            uint32_t value;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        static bool IsWrathModel(const std::vector<uint8_t>& bytes) noexcept
        {
            // 3.3.5a uses MD20 version 264, not the chunked retail MD21 format.
            return bytes.size() >= 304 && std::memcmp(bytes.data(), "MD20", 4) == 0 &&
                   ReadU32(bytes, 4) == 264;
        }

        static bool RangeFits(size_t offset, size_t count, size_t stride, size_t size) noexcept
        {
            return offset <= size && count <= (size - offset) / stride;
        }

        // Issue #2: DBC texture overrides use the subfolder, but hardcoded M2
        // texture paths did not. Append relocated names and update only their
        // descriptors: moving existing bytes would invalidate every M2 offset.
        static bool RelocateTextures(std::vector<uint8_t>& bytes, const std::string& modelPath)
        {
            const size_t originalSize = bytes.size();
            const uint32_t count = ReadU32(bytes, 80);
            const uint32_t textureOffset = ReadU32(bytes, 84);
            if (!RangeFits(textureOffset, count, 16, originalSize)) return false;
            size_t slash = modelPath.find_last_of('\\');
            std::string folder = slash == std::string::npos ? "" : modelPath.substr(0, slash + 1);
            std::vector<std::pair<size_t, std::string>> replacements;
            for (uint32_t i = 0; i < count; ++i)
            {
                const size_t descriptor = textureOffset + static_cast<size_t>(i) * 16;
                if (ReadU32(bytes, descriptor) != 0) continue; // replaceable textures have no filename
                uint32_t length = ReadU32(bytes, descriptor + 8);
                uint32_t offset = ReadU32(bytes, descriptor + 12);
                if (!length) continue;
                if (!RangeFits(offset, length, 1, originalSize)) return false;
                const char* name = reinterpret_cast<const char*>(bytes.data() + offset);
                const char* end = static_cast<const char*>(std::memchr(name, '\0', length));
                if (!end) return false;
                std::string path = NormalizePath(std::string(name, end).c_str());
                size_t separator = path.find_last_of('\\');
                std::string base = path.substr(separator == std::string::npos ? 0 : separator + 1);
                if (base.empty()) return false;
                std::string relocated = folder + base;
                if (relocated.size() >= 264) return false;
                replacements.emplace_back(descriptor, std::move(relocated));
            }
            for (const auto& replacement : replacements)
            {
                if (replacement.second.size() + 1 >
                    (std::numeric_limits<uint32_t>::max)() - bytes.size()) return false;
                uint32_t offset = static_cast<uint32_t>(bytes.size());
                uint32_t length = static_cast<uint32_t>(replacement.second.size() + 1);
                bytes.insert(bytes.end(), replacement.second.begin(), replacement.second.end());
                bytes.push_back(0);
                std::memcpy(bytes.data() + replacement.first + 8, &length, sizeof(length));
                std::memcpy(bytes.data() + replacement.first + 12, &offset, sizeof(offset));
            }
            return true;
        }

        // ─── Client-side provider ─────────────────────────────────────────────

        static bool VirtualProvide(const char* name, std::vector<uint8_t>& out)
        {
            if (!name) return false;
            std::string key = NormalizePath(name);
            if (key.find("_wxl_") == std::string::npos) return false;
            VPathLog("  VirtualProvide: '%s'", name);
            std::lock_guard<std::mutex> lock(g_virtualMutex);
            auto it = g_virtualBytes.find(key);
            if (it == g_virtualBytes.end())
            {
                VPathLog("  VirtualProvide: NOT FOUND (table has %zu entries)", g_virtualBytes.size());
                return false;
            }
            VPathLog("  VirtualProvide: HIT (%zu bytes)", it->second.size());
            out = it->second;
            return true;
        }

        struct Registrar
        {
            Registrar() { wxl::runtime::storage::RegisterClientProvider(&VirtualProvide); }
        };
        static Registrar g_registrar;
    }

    // ─── Public API ───────────────────────────────────────────────────────────

    size_t VPathBuildKey(char* out, size_t outSz, void* cmo,
                         const char* realMdxPath,
                         const uint16_t* geoIds, uint32_t geoCount,
                         const char* texPath, bool relocateEmbeddedTextures,
                         uint32_t attachmentId)
    {
        uint16_t sorted[16];
        uint32_t n = 0;
        if (!SortIds(sorted, n, geoIds, geoCount))
        {
            if (out && outSz) out[0] = '\0';
            return 0;
        }
        return BuildKey(out, outSz, cmo, realMdxPath, sorted, n, texPath,
                        relocateEmbeddedTextures, attachmentId);
    }

    bool VPathPopulate(void* cmo, const char* realMdxPath,
                       const uint16_t* geoIds, uint32_t geoCount,
                       const char* texPath, bool relocateEmbeddedTextures,
                       uint32_t attachmentId)
    {
        uint16_t sorted[16];
        uint32_t n = 0;
        if (!SortIds(sorted, n, geoIds, geoCount)) return false;
        char vM2[264];
        if (!BuildKey(vM2, sizeof(vM2), cmo, realMdxPath, sorted, n, texPath,
                      relocateEmbeddedTextures, attachmentId)) return false;

        // No-op if already populated (same permutation on a re-equip cycle).
        {
            std::lock_guard<std::mutex> lock(g_virtualMutex);
            if (g_virtualBytes.count(vM2)) return true;
        }

        std::string normPath = NormalizePath(realMdxPath);
        size_t extension = ExtensionOffset(normPath);
        if (normPath.compare(extension, std::string::npos, ".mdx") == 0)
            normPath.replace(extension, std::string::npos, ".m2");
        if (normPath.size() >= 264) return false;

        std::vector<uint8_t> m2Bytes;
        if (!ReadGameFile(normPath.c_str(), m2Bytes) || !IsWrathModel(m2Bytes))
        {
            VPathLog("  VPathPopulate: unreadable or non-3.3.5a M2 '%s'", normPath.c_str());
            return false;
        }
        uint32_t skinCount = ReadU32(m2Bytes, 68);
        if (skinCount > 4) return false; // Wrath supports at most four skin profiles.
        if (relocateEmbeddedTextures && !RelocateTextures(m2Bytes, normPath))
        {
            VPathLog("  VPathPopulate: invalid texture descriptors '%s'", normPath.c_str());
            return false;
        }
        std::vector<std::pair<std::string, std::vector<uint8_t>>> skins;
        for (uint32_t i = 0; i < skinCount; ++i)
        {
            char realSkin[264], virtualSkin[264];
            if (!SkinPath(realSkin, sizeof(realSkin), normPath.c_str(), i) ||
                !SkinPath(virtualSkin, sizeof(virtualSkin), vM2, i)) return false;
            std::vector<uint8_t> skinBytes;
            if (!ReadGameFile(realSkin, skinBytes) || skinBytes.size() < 48 ||
                std::memcmp(skinBytes.data(), "SKIN", 4) != 0)
            {
                VPathLog("  VPathPopulate: unreadable or invalid skin '%s'", realSkin);
                return false;
            }
            skins.emplace_back(virtualSkin, std::move(skinBytes));
        }

        // Publish only complete sets. Failed reads can be retried after fixing assets.
        VPathLog("  VPathPopulate: '%s' -> '%s' (%zu bytes, %u skins)",
                 normPath.c_str(), vM2, m2Bytes.size(), skinCount);
        std::lock_guard<std::mutex> lock(g_virtualMutex);
        // Another population may have completed while files were being read.
        if (g_virtualBytes.count(vM2)) return true;
        g_virtualBytes.emplace(vM2, std::move(m2Bytes));
        auto& paths = g_cmoVPaths[cmo];
        paths.emplace_back(vM2);
        for (auto& skin : skins)
        {
            paths.emplace_back(skin.first);
            g_virtualBytes.emplace(std::move(skin.first), std::move(skin.second));
        }
        return true;
    }

    void VPathEvictCmo(void* cmo)
    {
        std::lock_guard<std::mutex> lock(g_virtualMutex);
        auto it = g_cmoVPaths.find(cmo);
        if (it == g_cmoVPaths.end()) return;
        for (const auto& path : it->second)
            g_virtualBytes.erase(path);
        g_cmoVPaths.erase(it);
    }
}
