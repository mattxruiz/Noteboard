#pragma once

#include "Models.h"
#include <string>
#include <vector>

class Storage {
public:
    explicit Storage(std::wstring rootDir);

    bool load(std::vector<Project>& outProjects);
    bool save(const std::vector<Project>& projects);

    std::wstring projectDir(const std::string& projectId) const;
    std::wstring entryFilePath(const std::string& projectId, const std::string& entryId, const std::wstring& ext) const;
    bool ensureDirs() const;
    bool copyFileToEntry(const std::wstring& sourceFile, const std::string& projectId,
                         const std::string& entryId, std::wstring& outPath);

    const std::wstring& root() const { return root_; }

private:
    std::wstring root_;
    std::wstring indexPath() const;
};
