#ifndef ENGINE_UI_UIFILES_H
#define ENGINE_UI_UIFILES_H

#include <Engine/Filesystem/Directory.h>
#include <Engine/Filesystem/Path.h>
#include <Engine/Includes/Standard.h>

// The editor lists folders as "<path>/<name>" strings it owns and frees as it
// goes. The engine hands listings back as filesystem paths; these turn one into
// the other, so the editor's own bookkeeping is unchanged.
class UIFiles {
private:
    static vector<char*> ToStrings(const std::vector<std::filesystem::path>& paths) {
        vector<char*> strings;

        for (size_t i = 0; i < paths.size(); i++) {
            std::string path = Path::ToString(paths[i]);

            char* copy = (char*)malloc(path.size() + 1);
            if (!copy)
                continue;

            memcpy(copy, path.c_str(), path.size() + 1);
            strings.push_back(copy);
        }

        return strings;
    }

public:
    static vector<char*> GetFiles(const char* path, const char* searchPattern, bool allDirs) {
        std::vector<std::filesystem::path> files;
        Directory::GetFiles(&files, path, searchPattern, allDirs);
        return UIFiles::ToStrings(files);
    }

    static vector<char*> GetDirectories(const char* path, const char* searchPattern, bool allDirs) {
        std::vector<std::filesystem::path> directories;
        Directory::GetDirectories(&directories, path, searchPattern, allDirs);
        return UIFiles::ToStrings(directories);
    }
};

#endif /* ENGINE_UI_UIFILES_H */
