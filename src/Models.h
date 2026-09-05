#pragma once

#include <string>
#include <vector>
#include <cstdint>

enum class EntryType { Text, Image, File };

struct Entry {
    std::string id;
    std::string title;
    EntryType type = EntryType::Text;
    std::string body;          // note text / caption / file notes
    std::wstring filePath;     // image or attached file on disk
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
};

struct Project {
    std::string id;
    std::string name;
    std::string description;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::vector<Entry> entries;
};
