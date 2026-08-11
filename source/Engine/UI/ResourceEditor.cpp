#if INTERFACE
#include <Engine/Includes/Standard.h>

class ResourceEditor {
public:
    // What a pending confirmation is guarding, so the answer can be acted on.
    enum PendingAction {
        PENDING_NONE,
        PENDING_OPEN_SCENE,
        PENDING_CLOSE_PROJECT,
        PENDING_OPEN_PROJECT
    };
};
#endif

#include <Engine/UI/ResourceEditor.h>
#include <Engine/UI/SceneEditor.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UITheme.h>

#include <Engine/Application.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Filesystem/File.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/Scene.h>
#include <Engine/Utilities/StringUtils.h>

// The resource side of the editor.
//
// HatchStudio hangs every editor off a ResourceEditor base class that owns the
// file being edited: where it came from, whether it has unsaved changes, and
// the prompts that stop those changes being thrown away. The same bookkeeping
// lives here, with the scene editor as the document it looks after, alongside a
// browser over the project's Resources tree.

struct ResourceEntry {
    std::string Path;      // relative to Resources/
    std::string Name;
    size_t      Size = 0;
    bool        IsFolder = false;
    int         Depth = 0;
};

static vector<ResourceEntry> Entries;
static bool   Scanned = false;
static int    SelectedEntry = -1;

// The confirmation shown before something would discard unsaved scene edits.
static int    Pending = ResourceEditor::PENDING_NONE;
static char   PendingArgument[1024] = "";
static bool   Confirming = false;

static char   StatusText[256] = "";

PRIVATE STATIC void ResourceEditor::SetStatus(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(StatusText, sizeof(StatusText), format, args);
    va_end(args);
}

// ------------------------------------------------------------- scanning ----

PRIVATE STATIC const char* ResourceEditor::DescribeFile(const char* name) {
    struct {
        const char* Extension;
        const char* Description;
    } kinds[] = {
        { ".tmx",  "Tiled scene" },
        { ".hscn", "Hatch scene" },
        { ".bin",  "binary data" },
        { ".png",  "image" },
        { ".gif",  "image" },
        { ".jpg",  "image" },
        { ".ogg",  "audio" },
        { ".wav",  "audio" },
        { ".hsl",  "script source" },
        { ".ibc",  "compiled script" },
        { ".xml",  "configuration" },
        { ".json", "data" },
        { ".ini",  "settings" },
    };

    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (StringUtils::StrCaseStr(name, kinds[i].Extension))
            return kinds[i].Description;
    }

    return "file";
}

// Walks one folder of the Resources tree, deepest-first so the listing reads
// like a tree without needing one.
PRIVATE STATIC void ResourceEditor::ScanFolder(const char* path, const char* relative, int depth) {
    // Deep trees would make the list unreadable long before they ran out.
    if (depth > 6)
        return;

    vector<char*> folders = Directory::GetDirectories(path, "*", false);
    for (size_t i = 0; i < folders.size(); i++) {
        const char* name = strrchr(folders[i], '/');
        name = name ? name + 1 : folders[i];

        ResourceEntry entry;
        entry.Name = name;
        entry.Path = relative[0] ? std::string(relative) + "/" + name : name;
        entry.IsFolder = true;
        entry.Depth = depth;
        Entries.push_back(entry);

        ResourceEditor::ScanFolder(folders[i], entry.Path.c_str(), depth + 1);

        free(folders[i]);
    }

    vector<char*> files = Directory::GetFiles(path, "*", false);
    for (size_t i = 0; i < files.size(); i++) {
        const char* name = strrchr(files[i], '/');
        name = name ? name + 1 : files[i];

        ResourceEntry entry;
        entry.Name = name;
        entry.Path = relative[0] ? std::string(relative) + "/" + name : name;
        entry.Depth = depth;

        char* contents = NULL;
        entry.Size = File::ReadAllBytes(files[i], &contents);
        if (contents)
            free(contents);

        Entries.push_back(entry);

        free(files[i]);
    }
}

PUBLIC STATIC void ResourceEditor::Rescan() {
    Entries.clear();
    SelectedEntry = -1;
    Scanned = true;

    if (!ResourceManager::UsingDataFolder || !Directory::Exists("Resources")) {
        return;
    }

    ResourceEditor::ScanFolder("Resources", "", 0);
}

PUBLIC STATIC void ResourceEditor::Invalidate() {
    Scanned = false;
}

// ----------------------------------------------------------- guarding -----

PUBLIC STATIC bool ResourceEditor::HasUnsavedChanges() {
    return SceneEditor::UnsavedChanges;
}

// Asks to run an action that would lose unsaved scene edits. Returns true when
// it can go ahead now; otherwise a confirmation is put on screen and the caller
// will be told the answer through ResourceEditor::TakeApprovedAction.
PUBLIC STATIC bool ResourceEditor::RequestAction(int action, const char* argument) {
    if (!ResourceEditor::HasUnsavedChanges())
        return true;

    Pending = action;
    StringUtils::Copy(PendingArgument, argument ? argument : "", sizeof(PendingArgument));
    Confirming = true;

    return false;
}

PUBLIC STATIC bool ResourceEditor::IsConfirming() {
    return Confirming;
}

// Hands back an action the user agreed to lose their changes for, once.
PUBLIC STATIC int ResourceEditor::TakeApprovedAction(char* outArgument, size_t outSize) {
    if (Pending == ResourceEditor::PENDING_NONE || Confirming)
        return ResourceEditor::PENDING_NONE;

    int action = Pending;
    Pending = ResourceEditor::PENDING_NONE;

    if (outArgument)
        StringUtils::Copy(outArgument, PendingArgument, outSize);

    return action;
}

// The confirmation itself. Drawn like the file dialog: modal, over everything.
PUBLIC STATIC void ResourceEditor::DrawConfirmation() {
    if (!Confirming)
        return;

    UICore::Backdrop();

    float width = 60.0f * UIDraw::CharWidth();
    if (width > UIDraw::ViewWidth * 0.9f)
        width = UIDraw::ViewWidth * 0.9f;

    float height = UICore::TitleBarHeight() + UICore::RowHeight() * 4.0f + UICore::Pad() * 6.0f;

    float x = (UIDraw::ViewWidth - width) / 2.0f;
    float y = (UIDraw::ViewHeight - height) / 2.0f;

    UICore::BeginPanel("Unsaved Changes", x, y, width, height);
        UICore::Text("The scene has edits that have not been saved.", UI_COL_TEXT);
        UICore::Text("Going ahead will lose them.", UI_COL_TEXT_DIM);
        UICore::Separator();

        float buttonWidth = UICore::ContentWidth() / 3.0f - UICore::Pad();

        UICore::SetNextItemWidth(buttonWidth);
        if (UICore::ButtonEnabled("Save First", SceneEditor::CanSave())) {
            if (SceneEditor::Save())
                Confirming = false;
        }

        UICore::SameLine();
        UICore::SetNextItemWidth(buttonWidth);
        if (UICore::Button("Discard")) {
            SceneEditor::UnsavedChanges = false;
            Confirming = false;
        }

        UICore::SameLine();
        UICore::SetNextItemWidth(buttonWidth);
        if (UICore::Button("Cancel")) {
            Confirming = false;
            Pending = ResourceEditor::PENDING_NONE;
        }
    UICore::EndPanel();
}

// ------------------------------------------------------------- browser ----

PUBLIC STATIC void ResourceEditor::Draw(float x, float y, float width, float height, bool split) {
    if (!Scanned)
        ResourceEditor::Rescan();

    float halfW = split ? width / 2.0f : width;
    float halfH = split ? height : height / 2.0f;
    float secondX = split ? x + halfW : x;
    float secondY = split ? y : y + halfH;

    UICore::BeginPanel("Resources", x, y, halfW, halfH);
        if (!ResourceManager::UsingDataFolder) {
            UICore::Text("This project runs from a packed .hatch file,", UI_COL_TEXT_DIM);
            UICore::Text("so its resources cannot be browsed.", UI_COL_TEXT_FAINT);
        }
        else if (!Entries.size()) {
            UICore::Text("No Resources folder here.", UI_COL_TEXT_DIM);
        }
        else {
            UICore::ResetRowStriping();

            for (size_t i = 0; i < Entries.size(); i++) {
                ResourceEntry& entry = Entries[i];

                // Depth is shown with leading spaces, which keeps the row a
                // plain list item and still reads as a tree.
                char label[1200];
                snprintf(label, sizeof(label), "%*s%s%s##res%d",
                    entry.Depth * 2, "",
                    entry.IsFolder ? "[ " : "",
                    entry.IsFolder ? (entry.Name + " ]").c_str() : entry.Name.c_str(),
                    (int)i);

                if (UICore::ListItem(label, SelectedEntry == (int)i))
                    SelectedEntry = (int)i;
            }
        }
    UICore::EndPanel();

    UICore::BeginPanel("Resource", secondX, secondY, halfW, halfH);
        if (SelectedEntry >= 0 && SelectedEntry < (int)Entries.size()) {
            ResourceEntry& entry = Entries[SelectedEntry];

            UICore::Field("Name", entry.Name.c_str());
            UICore::Field("Path", entry.Path.c_str());
            UICore::Field("Kind", entry.IsFolder ? "folder" : ResourceEditor::DescribeFile(entry.Name.c_str()));

            if (!entry.IsFolder) {
                if (entry.Size >= 1024)
                    UICore::FieldFormatted("Size", "%.1f KB", entry.Size / 1024.0);
                else
                    UICore::FieldFormatted("Size", "%d bytes", (int)entry.Size);
            }

            UICore::Separator();

            bool isScene = !entry.IsFolder &&
                (StringUtils::StrCaseStr(entry.Name.c_str(), ".tmx") ||
                 StringUtils::StrCaseStr(entry.Name.c_str(), ".hscn") ||
                 StringUtils::StrCaseStr(entry.Name.c_str(), ".bin"));

            if (UICore::ButtonEnabled("Load As Scene", isScene)) {
                // Losing unsaved edits to the scene that is open needs asking
                // about first; the answer is picked up by the Studio.
                if (ResourceEditor::RequestAction(ResourceEditor::PENDING_OPEN_SCENE, entry.Path.c_str())) {
                    Application::QueueSceneChange(entry.Path.c_str());
                    ResourceEditor::SetStatus("Loading \"%s\"...", entry.Path.c_str());
                }
            }
        }
        else
            UICore::Text("Pick something from the list.", UI_COL_TEXT_FAINT);

        UICore::Separator();
        UICore::Heading("Open Document");
        UICore::Field("Scene", Scene::CurrentScene[0] ? Scene::CurrentScene : "(none)");
        UICore::Field("State", SceneEditor::UnsavedChanges ? "edited, not saved" : "saved");
        UICore::Field("Writable", SceneEditor::CanSave() ? "yes" : "no");

        if (UICore::ButtonEnabled("Save Scene", SceneEditor::CanSave() && SceneEditor::UnsavedChanges))
            SceneEditor::Save();

        if (UICore::Button("Rescan Resources"))
            ResourceEditor::Invalidate();

        if (StatusText[0]) {
            UICore::Separator();
            UICore::Text(StatusText, UI_COL_SUCCESS);
        }
    UICore::EndPanel();
}
