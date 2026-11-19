#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace SaveDiff {

using Json = nlohmann::json;

constexpr size_t kLongestValue = 96;
constexpr const char* kReceiptPrefix = "fleet:received:";

inline bool IsVolatileKey(const std::string& key) {
    static const std::unordered_set<std::string> keys = {
        "sohStats", "trackerData", "itemTrackerData", "hintTrackerData", "checksum",  "time",
        "dayTime",  "playTimer",   "filePlaytime",    "naviTimer",       "tatlTimer",
    };
    return keys.contains(key);
}

inline bool IsStringSet(const Json& value) {
    return value.is_array() && !value.empty() &&
           std::all_of(value.begin(), value.end(), [](const Json& member) { return member.is_string(); });
}

inline bool IsKeyedList(const Json& value) {
    return value.is_array() && !value.empty() && std::all_of(value.begin(), value.end(), [](const Json& member) {
               return member.is_object() && member.contains("key");
           });
}

inline std::string DescribeRecord(const Json& record) {
    const std::string key =
        record.at("key").is_string() ? record.at("key").get<std::string>() : record.at("key").dump();
    return record.contains("mod") && record.at("mod").is_string() ? record.at("mod").get<std::string>() + ":" + key
                                                                  : key;
}

inline Json Normalize(const Json& value) {
    if (value.is_object()) {
        Json members = Json::object();
        for (const auto& [key, member] : value.items()) {
            if (!IsVolatileKey(key)) {
                members[key] = Normalize(member);
            }
        }
        return members;
    }
    if (IsStringSet(value)) {
        Json members = Json::object();
        for (const auto& member : value) {
            const std::string text = member.get<std::string>();
            if (text.rfind(kReceiptPrefix, 0) != 0) {
                members[text] = true;
            }
        }
        return members;
    }
    if (IsKeyedList(value)) {
        Json members = Json::object();
        for (const auto& record : value) {
            members[DescribeRecord(record)] = Normalize(record);
        }
        return members;
    }
    if (value.is_array()) {
        Json members = Json::array();
        for (const auto& member : value) {
            members.push_back(Normalize(member));
        }
        return members;
    }
    return value;
}

inline std::string Shorten(const Json& value) {
    std::string text = value.dump();
    if (text.size() > kLongestValue) {
        text.resize(kLongestValue);
        text += "...";
    }
    return text;
}

inline bool IsEmptyArray(const Json& value) {
    return value.is_array() && value.empty();
}

inline void CollectChanges(const Json& before, const Json& after, const std::string& path,
                           std::vector<std::string>& changes) {
    if (before == after) {
        return;
    }
    if (IsEmptyArray(before) && after.is_object()) {
        CollectChanges(Json::object(), after, path, changes);
        return;
    }
    if (before.is_object() && IsEmptyArray(after)) {
        CollectChanges(before, Json::object(), path, changes);
        return;
    }
    if (before.is_object() && after.is_object()) {
        for (const auto& [key, member] : after.items()) {
            const std::string child = path + "/" + key;
            if (before.contains(key)) {
                CollectChanges(before.at(key), member, child, changes);
            } else {
                changes.push_back("+ " + child + " = " + Shorten(member));
            }
        }
        for (const auto& [key, member] : before.items()) {
            if (!after.contains(key)) {
                changes.push_back("- " + path + "/" + key + " = " + Shorten(member));
            }
        }
        return;
    }
    if (before.is_array() && after.is_array() && before.size() == after.size()) {
        for (size_t index = 0; index < before.size(); index++) {
            CollectChanges(before[index], after[index], path + "/" + std::to_string(index), changes);
        }
        return;
    }
    changes.push_back("~ " + path + ": " + Shorten(before) + " -> " + Shorten(after));
}

inline std::filesystem::path FindSaveFile(const std::string& game, const std::filesystem::path& directory, int slot) {
    const std::string file = "file" + std::to_string(slot + 1);
    return game == "oot" ? directory / "Save" / (file + ".sav") : directory / "saves" / (file + ".json");
}

inline Json ReadState(const std::string& game, const std::filesystem::path& directory, int slot) {
    std::ifstream file(FindSaveFile(game, directory, slot));
    const Json saved = Json::parse(file, nullptr, false);
    if (!saved.is_object() || !saved.contains("sections")) {
        return Json();
    }
    if (game == "oot") {
        return Normalize(saved.at("sections"));
    }
    if (!saved.contains("owlSave") || !saved.at("sections").contains("owl")) {
        return Json();
    }
    return Normalize(
        Json{ { "save", saved.at("owlSave").at("save") }, { "sections", saved.at("sections").at("owl") } });
}

} // namespace SaveDiff
