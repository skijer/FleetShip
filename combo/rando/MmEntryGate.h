// What an OoT start must carry into MM's overworld, on top of reaching the Happy Mask Shop portal.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "SharedItems.h"

namespace ComboRando {

// Something to play, and the song that saves and rewinds the three days.
inline constexpr SharedFamily kMmEntryFamilies[] = { SF_OCARINA, SF_SONG_OF_TIME };

enum class MmOverworld { Open, BehindPortal, Shut };

inline bool IsNamedInDump(const nlohmann::json& dump, const char* list, const char* field, const std::string& name) {
    const nlohmann::json entries = dump.value(list, nlohmann::json::array());
    return std::any_of(entries.begin(), entries.end(),
                       [&](const nlohmann::json& entry) { return entry.value(field, std::string{}) == name; });
}

// An entry item MM starts with is absent from its dump: such a seed asks nothing for it.
inline std::vector<std::string> MmEntryItems(const std::string& mmDumpJson) {
    std::vector<std::string> entryItems;
    const nlohmann::json dump = nlohmann::json::parse(mmDumpJson, nullptr, false);
    if (!dump.is_object()) {
        return entryItems;
    }
    for (const SharedFamily family : kMmEntryFamilies) {
        const std::string name = SharedFamilyByIndex(family).mmName;
        if (IsNamedInDump(dump, "pool", "name", name) || IsNamedInDump(dump, "fixed", "item", name)) {
            entryItems.push_back(name);
        }
    }
    return entryItems;
}

// `mmSeen` is MM's view of the inventory, where a shared OoT copy already counts.
inline bool OwnsMmEntryItems(const std::vector<std::string>& entryItems, const std::vector<std::string>& mmSeen) {
    return std::all_of(entryItems.begin(), entryItems.end(), [&](const std::string& item) {
        return std::find(mmSeen.begin(), mmSeen.end(), item) != mmSeen.end();
    });
}

inline MmOverworld ResolveMmOverworld(bool isOpenFromStart, const std::vector<std::string>& entryItems,
                                      const std::vector<std::string>& mmSeen) {
    if (isOpenFromStart) {
        return MmOverworld::Open;
    }
    return OwnsMmEntryItems(entryItems, mmSeen) ? MmOverworld::BehindPortal : MmOverworld::Shut;
}

} // namespace ComboRando
