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
//
// Backing file: WTF\WXL\WarcraftXL.ini (relative to the client root). Distinct from
// wxl-dehardcoding's WTF\WXL\Settings.ini -- do not point another writer at that file.
// Temporary home: this belongs in core, not in an equipment-specific module, but is kept
// here for now per the module's current scope; see docs/todo/wxl-equip-extension-refactor.md.

#pragma once

namespace wxl::scripts::equipextension::settings
{
    /**
     * @brief A registered boolean setting, backed by WTF\WXL\WarcraftXL.ini.
     *
     * Construct one static instance per setting, e.g.:
     *   static BoolSetting g_enabled("MyModule", "Enabled", "What this does.", true);
     * On construction, writes the [namespace] section with the description as a leading
     * comment and the default value if the key is not already present, then reads and
     * caches the resolved value (existing file contents always win over the default).
     */
    class BoolSetting
    {
    public:
        BoolSetting(const char* ns, const char* key, const char* description, bool defaultValue) noexcept;

        bool Value() const noexcept { return value_; }

    private:
        bool value_;
    };
}
