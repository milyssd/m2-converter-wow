// Portable parsing and path rules for the 3.3.5a equipment extension.
// Copyright (C) 2026 WarcraftXL. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace wxl::scripts::equipextension::config
{
    struct GeosetFilter
    {
        uint16_t ids[16] = {};
        uint32_t count = 0;
    };

    struct SlotConfig
    {
        const char* folder;
        uint32_t defAttach1;
        uint32_t defAttach2;
        bool defUseRace;
        bool defUseGender;
    };

    // Internal slots follow the stock 3.3.5a slot dispatcher, not inventory order.
    inline constexpr SlotConfig kSlotConfig[11] = {
        { "Head",     11, 55, true,  true  },
        { "Shoulder",  6,  5, false, false },
        { "Shirt",    34, 34, false, false },
        { "Chest",    34, 34, false, false },
        { "Waist",    53, 53, false, false },
        { "Leg",       9, 10, false, false },
        { "Foot",     47, 48, false, false },
        { "Bracer",    3,  4, false, false },
        { "Glove",     1,  2, false, false },
        { "Tabard",   34, 34, false, false }, // 9: TABARD
        { "Cape",     12, 12, false, false }, // 10: BACK
    };

    inline constexpr uint32_t kUnhandledSlot = (std::numeric_limits<uint32_t>::max)();
    // 0..60 cover known stock/HD attachment-point IDs; UINT32_MAX disables a channel.
    inline constexpr uint32_t kMaxAttachmentId = 60;
    inline constexpr uint32_t kEquipToModelSlot[19] = {
        0, kUnhandledSlot, 1, 2, 3, 4, 5, 6, 7, 8,
        kUnhandledSlot, kUnhandledSlot, kUnhandledSlot, kUnhandledSlot,
        10, // Inventory slot 14: BACK
        kUnhandledSlot, kUnhandledSlot, kUnhandledSlot,
        9,  // Inventory slot 18: TABARD
    };

    namespace detail
    {
        inline bool IsSeparator(char c) noexcept { return c == '\\' || c == '/'; }
        inline char LowerAscii(char c) noexcept
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
        }

        inline bool EndsWith(std::string_view value, std::string_view suffix) noexcept
        {
            if (value.size() < suffix.size()) return false;
            const size_t start = value.size() - suffix.size();
            for (size_t i = 0; i < suffix.size(); ++i)
                if (LowerAscii(value[start + i]) != LowerAscii(suffix[i])) return false;
            return true;
        }

        // Reject absolute paths, traversal, empty components, and Windows path syntax.
        inline bool ValidRelativePath(std::string_view path) noexcept
        {
            if (path.empty() || IsSeparator(path.front()) || IsSeparator(path.back())) return false;
            size_t start = 0;
            for (size_t i = 0; i <= path.size(); ++i)
            {
                if (i < path.size() && !IsSeparator(path[i]))
                {
                    const unsigned char c = static_cast<unsigned char>(path[i]);
                    if (c < 32 || c == 127 || c == ':' || c == '*' || c == '?' ||
                        c == '"' || c == '<' || c == '>' || c == '|') return false;
                    continue;
                }
                const auto component = path.substr(start, i - start);
                if (component.empty() || component == "." || component == ".." ||
                    component.back() == '.' || component.back() == ' ') return false;
                start = i + 1;
            }
            return true;
        }

        inline std::string_view BaseName(std::string_view path) noexcept
        {
            const size_t slash = path.find_last_of("\\/");
            return slash == std::string_view::npos ? path : path.substr(slash + 1);
        }

        inline std::string_view Directory(std::string_view path) noexcept
        {
            const size_t slash = path.find_last_of("\\/");
            return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
        }

        inline bool ModelStem(std::string_view name, std::string_view& stem) noexcept
        {
            if (!ValidRelativePath(name)) return false;
            stem = name;
            if (EndsWith(name, ".mdx")) stem.remove_suffix(4);
            else if (EndsWith(name, ".m2")) stem.remove_suffix(3);
            else if (BaseName(name).find('.') != std::string_view::npos) return false;
            return ValidRelativePath(stem);
        }

        inline std::string_view TextureStem(std::string_view name) noexcept
        {
            if (EndsWith(name, ".blp")) name.remove_suffix(4);
            return name;
        }

        inline bool ParseUnsigned(std::string_view field, uint32_t maximum,
                                  uint32_t& value, bool allowHex = false) noexcept
        {
            unsigned base = 10;
            if (allowHex && field.size() >= 2 && field[0] == '0' &&
                (field[1] == 'x' || field[1] == 'X'))
            {
                base = 16;
                field.remove_prefix(2);
            }
            if (field.empty()) return false;
            uint32_t parsed = 0;
            for (char c : field)
            {
                unsigned digit;
                if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
                else if (base == 16 && LowerAscii(c) >= 'a' && LowerAscii(c) <= 'f')
                    digit = static_cast<unsigned>(LowerAscii(c) - 'a' + 10);
                else return false;
                if (digit >= base || parsed > (maximum - digit) / base) return false;
                parsed = parsed * base + digit;
            }
            value = parsed;
            return true;
        }

        inline bool ParseAttachment(std::string_view field, uint32_t& left,
                                    uint32_t& right) noexcept
        {
            const size_t pipe = field.find('|');
            uint32_t a, b;
            if (!ParseUnsigned(field.substr(0, pipe), kUnhandledSlot, a)) return false;
            if (pipe == std::string_view::npos) b = a;
            else if (!ParseUnsigned(field.substr(pipe + 1), kUnhandledSlot, b)) return false;
            if ((a > kMaxAttachmentId && a != kUnhandledSlot) ||
                (b > kMaxAttachmentId && b != kUnhandledSlot)) return false;
            left = a;
            right = b;
            return true;
        }

        // Every append is checked before writing; failure returns an empty output.
        class PathWriter
        {
        public:
            PathWriter(char* buffer, size_t capacity) noexcept
                : buffer_(buffer), capacity_(capacity), valid_(buffer && capacity > 0)
            {
                if (valid_) buffer_[0] = '\0';
            }
            void Append(std::string_view value) noexcept
            {
                if (!valid_) return;
                if (value.size() >= capacity_ - used_) { valid_ = false; return; }
                for (char c : value) buffer_[used_++] = IsSeparator(c) ? '\\' : c;
            }
            bool Finish() noexcept
            {
                if (buffer_ && capacity_ > 0) buffer_[valid_ ? used_ : 0] = '\0';
                return valid_;
            }
        private:
            char* buffer_;
            size_t capacity_;
            size_t used_ = 0;
            bool valid_;
        };

        inline bool ClearPath(char* buffer, size_t capacity) noexcept
        {
            if (buffer && capacity > 0) buffer[0] = '\0';
            return false;
        }

        inline bool ValidSuffix(const char* suffix) noexcept
        {
            if (!suffix || !*suffix) return false;
            for (const char* c = suffix; *c; ++c)
                if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                      (*c >= '0' && *c <= '9') || *c == '_')) return false;
            return true;
        }

        inline void AppendSuffix(PathWriter& writer, const char* race, const char* gender,
                                 bool useRace, bool useGender, bool newSuffix) noexcept
        {
            if (!useRace && !useGender) return;
            writer.Append("_");
            if (useRace) writer.Append(race);
            if (useRace && useGender && newSuffix) writer.Append("_");
            if (useGender) writer.Append(gender);
        }

        inline const char* Folder(const char* slot, bool collection, const char* custom) noexcept
        {
            if (custom && *custom) return custom;
            return collection ? "Collections" : slot;
        }

        inline bool HasDirectoryPrefix(std::string_view path, std::string_view prefix) noexcept
        {
            if (prefix.empty() || path.size() <= prefix.size() ||
                !IsSeparator(path[prefix.size()])) return false;
            for (size_t i = 0; i < prefix.size(); ++i)
            {
                if (IsSeparator(path[i]) && IsSeparator(prefix[i])) continue;
                if (LowerAscii(path[i]) != LowerAscii(prefix[i])) return false;
            }
            return true;
        }
    }

    // At most 16 supplied IDs, each in the uint16_t range. Commit only a valid list.
    // An absent list means no filter; duplicate IDs do not consume output entries.
    inline bool ParseGeosetFilter(const char* spec, GeosetFilter& output) noexcept
    {
        GeosetFilter parsed;
        if (spec && *spec)
        {
            std::string_view remaining(spec);
            unsigned supplied = 0;
            while (true)
            {
                if (++supplied > 16) return false;
                const size_t comma = remaining.find(',');
                uint32_t id;
                if (!detail::ParseUnsigned(remaining.substr(0, comma),
                                          (std::numeric_limits<uint16_t>::max)(), id)) return false;
                bool present = false;
                for (uint32_t i = 0; i < parsed.count; ++i)
                    if (parsed.ids[i] == id) { present = true; break; }
                if (!present) parsed.ids[parsed.count++] = static_cast<uint16_t>(id);
                if (comma == std::string_view::npos) break;
                remaining.remove_prefix(comma + 1);
            }
        }
        output = parsed;
        return true;
    }

    // Optional fields preserve caller defaults. Invalid data never partially overrides them.
    inline bool ParseIcon2(const char* spec,
                           uint32_t* attachA_l, uint32_t* attachA_r,
                           uint32_t* attachB_l, uint32_t* attachB_r,
                           uint32_t* flags, char* customFolder, size_t customFolderSz) noexcept
    {
        if (!attachA_l || !attachA_r || !attachB_l || !attachB_r || !flags) return false;
        if (!spec || !*spec) return true;
        std::string_view fields[4];
        std::string_view remaining(spec);
        for (size_t i = 0; i < 4; ++i)
        {
            const size_t colon = remaining.find(':');
            if (i == 3 && colon != std::string_view::npos) return false;
            fields[i] = remaining.substr(0, colon);
            if (colon == std::string_view::npos) break;
            remaining.remove_prefix(colon + 1);
        }
        uint32_t al = *attachA_l, ar = *attachA_r;
        uint32_t bl = *attachB_l, br = *attachB_r, parsedFlags = *flags;
        if (!fields[0].empty() && !detail::ParseAttachment(fields[0], al, ar)) return false;
        if (!fields[1].empty() && !detail::ParseAttachment(fields[1], bl, br)) return false;
        if (!fields[2].empty() && !detail::ParseUnsigned(fields[2], kUnhandledSlot,
                                                      parsedFlags, true)) return false;
        if (!fields[3].empty() && (!detail::ValidRelativePath(fields[3]) ||
            !customFolder || customFolderSz == 0 || fields[3].size() >= customFolderSz)) return false;

        *attachA_l = al;
        *attachA_r = ar;
        *attachB_l = bl;
        *attachB_r = br;
        *flags = parsedFlags;
        if (!fields[3].empty())
        {
            std::memcpy(customFolder, fields[3].data(), fields[3].size());
            customFolder[fields[3].size()] = '\0';
        }
        return true;
    }

    inline bool BuildSlotPath(char* buffer, size_t capacity, const char* modelName,
                              const char* race, const char* gender, uint32_t flags,
                              const char* slotFolder, bool isCollection,
                              const char* customFolder = nullptr) noexcept
    {
        const bool useRace = (flags & 0x4) == 0;
        const bool useGender = (flags & 0x2) == 0;
        const char* folder = detail::Folder(slotFolder, isCollection, customFolder);
        std::string_view stem;
        if (!modelName || !folder || !detail::ValidRelativePath(folder) ||
            !detail::ModelStem(modelName, stem) ||
            (useRace && !detail::ValidSuffix(race)) ||
            (useGender && !detail::ValidSuffix(gender))) return detail::ClearPath(buffer, capacity);
        detail::PathWriter writer(buffer, capacity);
        writer.Append("Item\\ObjectComponents\\");
        writer.Append(folder);
        writer.Append("\\");
        writer.Append(stem);
        if (flags & 0x20)
        {
            writer.Append("\\");
            writer.Append(detail::BaseName(stem));
        }
        detail::AppendSuffix(writer, race, gender, useRace, useGender, (flags & 0x40) != 0);
        writer.Append(".mdx");
        return writer.Finish();
    }

    inline bool BuildTexPath(char* buffer, size_t capacity, const char* textureName,
                             const char* race, const char* gender, uint32_t flags,
                             const char* slotFolder, bool isCollection,
                             const char* customFolder = nullptr,
                             const char* modelStem = nullptr) noexcept
    {
        const bool useRace = (flags & 0x8) != 0;
        const bool useGender = (flags & 0x10) != 0;
        const char* folder = detail::Folder(slotFolder, isCollection, customFolder);
        if (!textureName || !folder || !detail::ValidRelativePath(folder) ||
            !detail::ValidRelativePath(textureName) ||
            (useRace && !detail::ValidSuffix(race)) ||
            (useGender && !detail::ValidSuffix(gender))) return detail::ClearPath(buffer, capacity);
        std::string_view texture = detail::TextureStem(textureName);
        if (!detail::ValidRelativePath(texture)) return detail::ClearPath(buffer, capacity);
        std::string_view subBase;
        if (flags & 0x20)
        {
            if (modelStem && *modelStem)
            {
                if (!detail::ModelStem(modelStem, subBase)) return detail::ClearPath(buffer, capacity);
            }
            else subBase = texture;
            // DBC texture names may repeat a directory already present in the model name.
            const std::string_view prefixes[] = { subBase, detail::Directory(subBase),
                                                 detail::BaseName(subBase) };
            for (auto prefix : prefixes)
                if (detail::HasDirectoryPrefix(texture, prefix))
                {
                    texture.remove_prefix(prefix.size() + 1);
                    break;
                }
        }
        detail::PathWriter writer(buffer, capacity);
        writer.Append("Item\\ObjectComponents\\");
        writer.Append(folder);
        writer.Append("\\");
        if (flags & 0x20) { writer.Append(subBase); writer.Append("\\"); }
        writer.Append(texture);
        detail::AppendSuffix(writer, race, gender, useRace, useGender, (flags & 0x40) != 0);
        writer.Append(".blp");
        return writer.Finish();
    }
}
