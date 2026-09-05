#include "Storage.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

void EscapeField(std::ostream& os, const std::string& v) {
    for (char c : v) {
        if (c == '\\') os << "\\\\";
        else if (c == '\n') os << "\\n";
        else if (c == '\r') os << "\\r";
        else os << c;
    }
}

std::string UnescapeField(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == '\\') out.push_back('\\');
            else out.push_back(n);
        } else {
            out.push_back(v[i]);
        }
    }
    return out;
}

bool ReadTextFile(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF) out.erase(0, 3);
    return true;
}

bool WriteTextFile(const fs::path& path, const std::string& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), (std::streamsize)data.size());
    return (bool)out;
}

std::int64_t ParseI64(const std::string& v) {
    if (v.empty()) return 0;
    try { return std::stoll(v); }
    catch (...) { return 0; }
}

EntryType ParseType(const std::string& v) {
    if (v == "image") return EntryType::Image;
    if (v == "file") return EntryType::File;
    return EntryType::Text;
}

const char* TypeStr(EntryType t) {
    switch (t) {
    case EntryType::Image: return "image";
    case EntryType::File: return "file";
    default: return "text";
    }
}

std::wstring MakeAbsolute(const fs::path& root, const std::wstring& maybeRel) {
    fs::path p(maybeRel);
    if (p.is_absolute()) return p.wstring();
    return (root / p).wstring();
}

} // namespace

Storage::Storage(std::wstring rootDir) : root_(std::move(rootDir)) {}

std::wstring Storage::indexPath() const {
    return (fs::path(root_) / L"index.dat").wstring();
}

std::wstring Storage::projectDir(const std::string& projectId) const {
    return (fs::path(root_) / Utf8ToWide(projectId)).wstring();
}

std::wstring Storage::entryFilePath(const std::string& projectId, const std::string& entryId, const std::wstring& ext) const {
    return (fs::path(projectDir(projectId)) / L"files" / (Utf8ToWide(entryId) + ext)).wstring();
}

bool Storage::ensureDirs() const {
    std::error_code ec;
    fs::create_directories(root_, ec);
    return !ec;
}

bool Storage::copyFileToEntry(const std::wstring& sourceFile, const std::string& projectId,
                              const std::string& entryId, std::wstring& outPath) {
    std::wstring ext = fs::path(sourceFile).extension().wstring();
    if (ext.empty()) ext = L".bin";
    outPath = entryFilePath(projectId, entryId, ext);
    std::error_code ec;
    fs::create_directories(fs::path(outPath).parent_path(), ec);
    if (ec) return false;
    fs::copy_file(sourceFile, outPath, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool Storage::load(std::vector<Project>& outProjects) {
    outProjects.clear();
    ensureDirs();

    std::string raw;
    if (!ReadTextFile(indexPath(), raw)) return true;

    std::istringstream ss(raw);
    std::string line;
    Project* cur = nullptr;
    Entry* curE = nullptr;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        if (line == "[project]") {
            outProjects.push_back({});
            cur = &outProjects.back();
            curE = nullptr;
            continue;
        }
        if (line == "[entry]" && cur) {
            cur->entries.push_back({});
            curE = &cur->entries.back();
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = UnescapeField(line.substr(eq + 1));

        if (curE) {
            if (key == "id") curE->id = val;
            else if (key == "title") curE->title = val;
            else if (key == "type") curE->type = ParseType(val);
            else if (key == "body") curE->body = val;
            else if (key == "image" || key == "file") curE->filePath = Utf8ToWide(val);
            else if (key == "created") curE->createdAt = ParseI64(val);
            else if (key == "updated") curE->updatedAt = ParseI64(val);
        } else if (cur) {
            if (key == "id") cur->id = val;
            else if (key == "name") cur->name = val;
            else if (key == "description") cur->description = val;
            else if (key == "created") cur->createdAt = ParseI64(val);
            else if (key == "updated") cur->updatedAt = ParseI64(val);
        }
    }

    fs::path root(root_);
    for (auto& p : outProjects) {
        for (auto& e : p.entries) {
            if (!e.filePath.empty())
                e.filePath = MakeAbsolute(root, e.filePath);
            // Legacy: image type without path still ok
            if (e.type == EntryType::Image && e.filePath.empty()) { /* skip */ }
            // Infer file type if marked image but extension isn't image? keep as stored
        }
    }
    return true;
}

bool Storage::save(const std::vector<Project>& projects) {
    ensureDirs();
    std::ostringstream out;
    out << "# Project Board state\n";

    for (const auto& p : projects) {
        out << "[project]\n";
        out << "id="; EscapeField(out, p.id); out << "\n";
        out << "name="; EscapeField(out, p.name); out << "\n";
        out << "description="; EscapeField(out, p.description); out << "\n";
        out << "created=" << p.createdAt << "\n";
        out << "updated=" << p.updatedAt << "\n";

        for (const auto& e : p.entries) {
            out << "[entry]\n";
            out << "id="; EscapeField(out, e.id); out << "\n";
            out << "title="; EscapeField(out, e.title); out << "\n";
            out << "type=" << TypeStr(e.type) << "\n";
            out << "body="; EscapeField(out, e.body); out << "\n";
            if (!e.filePath.empty()) {
                fs::path file(e.filePath);
                fs::path root(root_);
                std::error_code ec;
                auto rel = fs::relative(file, root, ec);
                std::string pathStr = WideToUtf8((!ec ? rel : file).wstring());
                out << "file="; EscapeField(out, pathStr); out << "\n";
            }
            out << "created=" << e.createdAt << "\n";
            out << "updated=" << e.updatedAt << "\n";
        }
    }

    return WriteTextFile(indexPath(), out.str());
}
