// wxl-equip-extension: small settings registry.
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

#include "Settings.hpp"

#include <windows.h>

#include <cstdio>

namespace wxl::scripts::equipextension::settings
{
    namespace
    {
        constexpr char kConfigDir[]  = "WTF\\WXL";
        constexpr char kConfigPath[] = "WTF\\WXL\\WarcraftXL.ini";
        // GetPrivateProfileIntA's default, distinct from any real bool value (0/1), used to
        // detect "key absent" instead of "key present and false".
        constexpr int kAbsentSentinel = 2;

        // Appends "[ns]\n; description\nkey=default\n" to the ini. Does not deduplicate
        // section headers across calls -- fine while each namespace registers one key today;
        // revisit if a namespace grows a second setting before this moves to core.
        void WriteDefault(const char* ns, const char* key, const char* description, bool defaultValue) noexcept
        {
            CreateDirectoryA("WTF", nullptr);
            CreateDirectoryA(kConfigDir, nullptr);
#pragma warning(suppress: 4996)
            FILE* f = std::fopen(kConfigPath, "a");
            if (!f) return;
            std::fprintf(f, "\n[%s]\n; %s\n%s=%d\n", ns, description, key, defaultValue ? 1 : 0);
            std::fclose(f);
        }
    }

    BoolSetting::BoolSetting(const char* ns, const char* key, const char* description, bool defaultValue) noexcept
    {
        const int existing = static_cast<int>(GetPrivateProfileIntA(ns, key, kAbsentSentinel, kConfigPath));
        if (existing == kAbsentSentinel)
        {
            WriteDefault(ns, key, description, defaultValue);
            value_ = defaultValue;
        }
        else
        {
            value_ = existing != 0;
        }
    }
}
