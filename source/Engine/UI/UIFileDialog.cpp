#if INTERFACE
#include <Engine/Includes/Standard.h>

class UIFileDialog {
public:
    enum Mode {
        PICK_FOLDER,
        PICK_FILE
    };

    enum Result {
        RESULT_NONE,
        RESULT_ACCEPTED,
        RESULT_CANCELLED
    };
};
#endif

#include <Engine/UI/UIFileDialog.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UITheme.h>

#include <Engine/Filesystem/Directory.h>
#include <Engine/Filesystem/File.h>
#include <Engine/Utilities/StringUtils.h>

// A modal file browser drawn by the engine itself.
//
// HatchStudio reaches for tinyfiledialogs to get a native picker; doing the
// same here would mean vendoring a C library and a per-platform backend for
// something the engine can already draw. This browses the filesystem with the
// engine's own Directory calls instead, so it needs nothing new and behaves the
// same everywhere the engine runs.

static bool  DialogOpen = false;
static int   DialogMode = UIFileDialog::PICK_FOLDER;
static char  DialogTitle[128] = "";
static char  DialogFilter[64] = "*";
static char  CurrentPath[2048] = ".";
static char  PathField[2048] = ".";
static char  SelectedPath[2048] = "";

static vector<std::string> Folders;
static vector<std::string> Files;
static int  SelectedRow = -1;
static bool SelectedIsFolder = false;
static bool NeedsRefresh = true;

// Strips "." and collapses ".." so the path shown stays readable after a few
// trips up and down the tree.
PRIVATE STATIC void UIFileDialog::NormalizePath(char* path) {
    vector<std::string> parts;
    bool absolute = path[0] == '/' || path[0] == '\\';

    char* cursor = path;
    while (*cursor) {
        char* start = cursor;
        while (*cursor && *cursor != '/' && *cursor != '\\')
            cursor++;

        std::string part(start, (size_t)(cursor - start));
        if (part == ".." && parts.size() && parts.back() != "..")
            parts.pop_back();
        else if (part.size() && part != ".")
            parts.push_back(part);

        if (*cursor)
            cursor++;
    }

    std::string joined = absolute ? "/" : "";
    for (size_t i = 0; i < parts.size(); i++) {
        if (i)
            joined += "/";
        joined += parts[i];
    }

    if (!joined.size())
        joined = absolute ? "/" : ".";

    StringUtils::Copy(path, joined.c_str(), 2048);
}

PRIVATE STATIC void UIFileDialog::Refresh() {
    Folders.clear();
    Files.clear();
    SelectedRow = -1;
    NeedsRefresh = false;

    if (!Directory::Exists(CurrentPath))
        return;

    // Directory::GetDirectories and GetFiles hand back "<path>/<name>" strings
    // that the caller owns; only the name is wanted here.
    vector<char*> directories = Directory::GetDirectories(CurrentPath, "*", false);
    for (size_t i = 0; i < directories.size(); i++) {
        const char* name = strrchr(directories[i], '/');
        Folders.push_back(std::string(name ? name + 1 : directories[i]));
        free(directories[i]);
    }

    if (DialogMode == UIFileDialog::PICK_FILE) {
        vector<char*> files = Directory::GetFiles(CurrentPath, DialogFilter, false);
        for (size_t i = 0; i < files.size(); i++) {
            const char* name = strrchr(files[i], '/');
            Files.push_back(std::string(name ? name + 1 : files[i]));
            free(files[i]);
        }
    }

    std::sort(Folders.begin(), Folders.end());
    std::sort(Files.begin(), Files.end());
}

PUBLIC STATIC void UIFileDialog::Open(const char* title, int mode, const char* startPath, const char* filter) {
    StringUtils::Copy(DialogTitle, title, sizeof(DialogTitle));
    StringUtils::Copy(DialogFilter, filter && *filter ? filter : "*", sizeof(DialogFilter));
    StringUtils::Copy(CurrentPath, startPath && *startPath ? startPath : ".", sizeof(CurrentPath));

    UIFileDialog::NormalizePath(CurrentPath);
    StringUtils::Copy(PathField, CurrentPath, sizeof(PathField));

    DialogMode = mode;
    DialogOpen = true;
    NeedsRefresh = true;
    SelectedPath[0] = '\0';
}

PUBLIC STATIC bool UIFileDialog::IsOpen() {
    return DialogOpen;
}

PUBLIC STATIC void UIFileDialog::Close() {
    DialogOpen = false;
    Folders.clear();
    Files.clear();
}

// The path the user settled on, valid after Draw returns RESULT_ACCEPTED.
PUBLIC STATIC const char* UIFileDialog::GetSelectedPath() {
    return SelectedPath;
}

// Sticks a name onto a folder path. The join happens in a buffer big enough to
// hold both sides so the result is only shortened on the way out, never mangled
// half way through.
PRIVATE STATIC void UIFileDialog::JoinPath(char* out, size_t outSize, const char* base, const char* name) {
    char joined[4200];
    snprintf(joined, sizeof(joined), "%s/%s", base, name);
    StringUtils::Copy(out, joined, outSize);
}

PRIVATE STATIC void UIFileDialog::EnterFolder(const char* name) {
    UIFileDialog::JoinPath(CurrentPath, sizeof(CurrentPath), CurrentPath, name);
    UIFileDialog::NormalizePath(CurrentPath);
    StringUtils::Copy(PathField, CurrentPath, sizeof(PathField));

    NeedsRefresh = true;
}

// Draws the dialog and reports what the user did with it. Call with no panel
// open, after everything the dialog covers has been drawn.
PUBLIC STATIC int UIFileDialog::Draw() {
    if (!DialogOpen)
        return UIFileDialog::RESULT_NONE;

    if (NeedsRefresh)
        UIFileDialog::Refresh();

    UICore::Backdrop();

    float width = UIDraw::ViewWidth * 0.7f;
    float height = UIDraw::ViewHeight * 0.7f;

    float minimumWidth = UIDraw::CharWidth() * 44.0f;
    if (width < minimumWidth)
        width = UIDraw::ViewWidth;
    if (height < UICore::RowHeight() * 8.0f)
        height = UIDraw::ViewHeight;

    float x = (UIDraw::ViewWidth - width) / 2.0f;
    float y = (UIDraw::ViewHeight - height) / 2.0f;

    int result = UIFileDialog::RESULT_NONE;

    float footerHeight = UICore::RowHeight() * 2.0f + UICore::Pad() * 4.0f;

    UICore::BeginPanel(DialogTitle, x, y, width, height - footerHeight);
        UICore::ResetRowStriping();

        if (strcmp(CurrentPath, "/") != 0) {
            if (UICore::ListItem("..  (up one folder)##dialogup", false))
                UIFileDialog::EnterFolder("..");
        }

        for (size_t i = 0; i < Folders.size(); i++) {
            char label[2100];
            snprintf(label, sizeof(label), "[ %s ]##dialogdir%d", Folders[i].c_str(), (int)i);

            if (UICore::ListItem(label, SelectedIsFolder && SelectedRow == (int)i)) {
                // First click selects, so a folder can be chosen in folder
                // mode; the Open button steps into it.
                SelectedRow = (int)i;
                SelectedIsFolder = true;
            }
        }

        for (size_t i = 0; i < Files.size(); i++) {
            char label[2100];
            snprintf(label, sizeof(label), "%s##dialogfile%d", Files[i].c_str(), (int)i);

            if (UICore::ListItem(label, !SelectedIsFolder && SelectedRow == (int)i)) {
                SelectedRow = (int)i;
                SelectedIsFolder = false;
            }
        }

        if (!Folders.size() && !Files.size())
            UICore::Text("Nothing here.", UI_COL_TEXT_FAINT);
    UICore::EndPanel();

    UICore::BeginPanel("##dialogfooter", x, y + height - footerHeight, width, footerHeight);
        // Typing a path directly is quicker than clicking down a deep tree.
        if (UICore::TextField("", PathField, sizeof(PathField))) {
            StringUtils::Copy(CurrentPath, PathField, sizeof(CurrentPath));
            UIFileDialog::NormalizePath(CurrentPath);
            NeedsRefresh = true;
        }

        float buttonWidth = UICore::ContentWidth() / 3.0f - UICore::Pad();

        bool folderPicked = SelectedIsFolder && SelectedRow >= 0 && SelectedRow < (int)Folders.size();
        bool filePicked = !SelectedIsFolder && SelectedRow >= 0 && SelectedRow < (int)Files.size();

        UICore::SetNextItemWidth(buttonWidth);
        if (UICore::ButtonEnabled("Enter Folder", folderPicked)) {
            UIFileDialog::EnterFolder(Folders[SelectedRow].c_str());
            SelectedIsFolder = false;
            SelectedRow = -1;
        }

        UICore::SameLine();
        UICore::SetNextItemWidth(buttonWidth);

        if (DialogMode == UIFileDialog::PICK_FOLDER) {
            // Choosing a folder takes either the highlighted one or, with
            // nothing highlighted, the folder being looked at.
            if (UICore::Button("Choose")) {
                if (folderPicked)
                    UIFileDialog::JoinPath(SelectedPath, sizeof(SelectedPath),
                        CurrentPath, Folders[SelectedRow].c_str());
                else
                    StringUtils::Copy(SelectedPath, CurrentPath, sizeof(SelectedPath));

                UIFileDialog::NormalizePath(SelectedPath);
                result = UIFileDialog::RESULT_ACCEPTED;
            }
        }
        else if (UICore::ButtonEnabled("Open", filePicked)) {
            UIFileDialog::JoinPath(SelectedPath, sizeof(SelectedPath),
                CurrentPath, Files[SelectedRow].c_str());

            UIFileDialog::NormalizePath(SelectedPath);
            result = UIFileDialog::RESULT_ACCEPTED;
        }

        UICore::SameLine();
        UICore::SetNextItemWidth(buttonWidth);
        if (UICore::Button("Cancel"))
            result = UIFileDialog::RESULT_CANCELLED;
    UICore::EndPanel();

    if (result != UIFileDialog::RESULT_NONE)
        UIFileDialog::Close();

    return result;
}
