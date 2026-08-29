#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>

class Studio {
public:
    static bool Visible;
    static bool PauseGameWhileOpen;
};
#endif

#include <Engine/UI/Studio.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UIFileDialog.h>
#include <Engine/UI/UIMenu.h>
#include <Engine/UI/UITheme.h>
#include <Engine/UI/ResourceEditor.h>
#include <Engine/UI/SceneEditor.h>
#include <Engine/UI/TileCollisionEditor.h>

#include <Engine/Bytecode/SourceFileMap.h>

#include <Engine/Application.h>
#include <Engine/Graphics.h>
#include <Engine/InputManager.h>
#include <Engine/Scene.h>
#include <Engine/Exporters/MegaDriveExporter.h>
#include <Engine/Exporters/Sega32XExporter.h>
#include <Engine/Exporters/MegaCDExporter.h>
#include <Engine/Exporters/GameGearExporter.h>
#include <Engine/Exporters/SegaSaturnExporter.h>
#include <Engine/UI/Hierarchy.h>
#include <Engine/UI/Inspector.h>
#include <Engine/UI/Selection.h>
#include <Engine/Audio/AudioManager.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Filesystem/File.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/ResourceTypes/SceneFormats/TiledMapWriter.h>
#include <Engine/UI/SceneEditor3D.h>
#include <Engine/Scene/SceneInfo.h>
#include <Engine/Types/Tileset.h>
#include <Engine/Utilities/StringUtils.h>

// The editor front end. Everything the engine used to need a command line, a
// text editor and a restart for -- picking a project, browsing its scenes,
// pausing and stepping the game, changing settings, reading the log -- is
// reachable from here.

bool Studio::Visible = false;
bool Studio::PauseGameWhileOpen = true;

enum StudioTab {
    STUDIO_TAB_PROJECT,
    STUDIO_TAB_SCENES,
    STUDIO_TAB_HIERARCHY,
    STUDIO_TAB_EDITOR,
    STUDIO_TAB_COLLISION,
    STUDIO_TAB_3D,
    STUDIO_TAB_RESOURCES,
    STUDIO_TAB_PLAY,
    STUDIO_TAB_SETTINGS,
    STUDIO_TAB_CONSOLE,
    STUDIO_TAB_HELP,
    STUDIO_TAB_COUNT
};

static const char* TabNames[STUDIO_TAB_COUNT] = {
    "Project", "Scenes", "Hierarchy", "Editor", "Collision", "3D", "Resources",
    "Play", "Settings", "Console", "Help"
};

// Widgets are drawn and clicked in the same call, which means a button's
// handler runs half way through building the frame. Anything that tears the
// engine down and puts it back together -- opening a project, restarting,
// resizing the window -- would pull the renderer's state out from under the
// rest of that frame, so those are recorded here and carried out by
// Studio::Update at the top of the next one.
enum StudioAction {
    STUDIO_ACTION_NONE,
    STUDIO_ACTION_OPEN_PROJECT,
    STUDIO_ACTION_CLOSE_PROJECT,
    STUDIO_ACTION_RESTART_ENGINE,
    STUDIO_ACTION_RELOAD_SCENE,
    STUDIO_ACTION_RESTART_SCENE,
    STUDIO_ACTION_SET_WINDOW_SIZE,
    STUDIO_ACTION_SET_FULLSCREEN,
    STUDIO_ACTION_RUN
};

// Which file the browser was opened for, since one dialog serves several
// commands.
enum StudioBrowseFor {
    STUDIO_BROWSE_NONE,
    STUDIO_BROWSE_PROJECT_FOLDER,
    STUDIO_BROWSE_DATA_FILE,
    STUDIO_BROWSE_SCENE_FILE,
    STUDIO_BROWSE_SCRIPTS_FOLDER
};

static int  PendingAction = STUDIO_ACTION_NONE;
static char PendingPath[4096] = "";
static int  PendingValueA = 0;
static int  PendingValueB = 0;

static int  CurrentTab = STUDIO_TAB_PROJECT;

// Where a SEGA export writes to. Relative paths land beside the project, which
// is where someone would look for it.
static char SegaExportPath[512] = "SegaExport";
static bool Initialized = false;

// Project browser.
static vector<std::string> FoundProjects;
static int  SelectedProject = -1;
static bool ProjectsScanned = false;
static char NewProjectName[128] = "MyGame";

// Scene browser. Scenes come from the project's scene list when it has one, and
// from whatever is sitting in Resources/Scenes when it doesn't.
static int  SelectedScene = -1;
static vector<std::string> SceneFiles;
static bool SceneFilesScanned = false;

// New scenes. The tile size starts at what Hatch scenes usually use and follows
// the loaded scene once there is one to follow.
static char NewSceneName[128] = "MyScene";
static int  NewSceneWidth = 40;
static int  NewSceneHeight = 30;
static int  NewSceneTileWidth = 16;
static int  NewSceneTileHeight = 16;
static int  NewSceneLayers = 2;
static bool NewSceneTakeTilesets = true;
static bool NewSceneOpen = true;
static std::string NewScenePending;

// Console filters.
static bool ShowVerbose = false;
static bool ShowInfo = true;
static bool ShowWarnings = true;
static bool ShowErrors = true;
static bool FollowLog = true;
static size_t LastLogCount = 0;

// Settings staging. Window size is applied on demand rather than as it's typed.
static char WindowWidthText[16] = "";
static char WindowHeightText[16] = "";

static char   StatusMessage[256] = "";
static Uint32 StatusMessageTime = 0;

static int  BrowsingFor = STUDIO_BROWSE_NONE;

// "Run" either goes back to the game's start scene or picks up whatever scene
// is loaded, the same choice HatchStudio offers under Set Run Start Scene.
static bool RunFromStartScene = true;

// Recent projects, kept in the settings file and offered in the File menu.
static vector<std::string> RecentProjects;

static char ScriptsFolderText[1024] = "Scripts";

// Read again when the dropdown list is painted, so it has to be static.
static const char* const RendererNames[] = { "opengl", "sdl2", "direct3d", "metal" };
static const int RendererCount = (int)(sizeof(RendererNames) / sizeof(RendererNames[0]));

PRIVATE STATIC void Studio::SetStatus(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(StatusMessage, sizeof(StatusMessage), format, args);
    va_end(args);

    StatusMessageTime = SDL_GetTicks();
}

// --------------------------------------------------------------- lifecycle -

PUBLIC STATIC void Studio::Init() {
    if (Initialized)
        return;

    UIDraw::Init();

    snprintf(WindowWidthText, sizeof(WindowWidthText), "%d", Application::WindowWidth);
    snprintf(WindowHeightText, sizeof(WindowHeightText), "%d", Application::WindowHeight);

    if (Application::Settings) {
        Application::Settings->GetBool("studio", "pauseWhenOpen", &Studio::PauseGameWhileOpen);

        Application::Settings->GetBool("studio", "runFromStartScene", &RunFromStartScene);

        bool openOnStart = false;
        Application::Settings->GetBool("studio", "openOnStart", &openOnStart);
        if (openOnStart && Application::IsPC())
            Studio::Visible = true;
    }

    StringUtils::Copy(ScriptsFolderText, SourceFileMap::Path, sizeof(ScriptsFolderText));

    Studio::LoadRecentProjects();

    Initialized = true;
}

PUBLIC STATIC void Studio::Dispose() {
    UIDraw::Dispose();

    FoundProjects.clear();
    SceneFiles.clear();

    Initialized = false;
}

PUBLIC STATIC bool Studio::IsAvailable() {
    // Nothing here makes sense without a mouse and a resizable window. A
    // browser has both, but it is not a PC as far as IsPC is concerned: that
    // one answers the question scripts ask about the operating system, and the
    // browser is not one of the three it names.
    return Initialized &&
        (Application::IsPC() || Application::Platform == Platforms::Web);
}

PUBLIC STATIC void Studio::Show() {
    if (!Studio::IsAvailable())
        return;

    Studio::Visible = true;
    UICore::Reset();

    ProjectsScanned = false;
    SceneFilesScanned = false;
}

PUBLIC STATIC void Studio::Hide() {
    Studio::Visible = false;
    UICore::Reset();
}

PUBLIC STATIC void Studio::Toggle() {
    if (Studio::Visible)
        Studio::Hide();
    else
        Studio::Show();
}

// True when the game should hold still because the editor is in front of it.
PUBLIC STATIC bool Studio::IsPausingGame() {
    return Studio::Visible && Studio::PauseGameWhileOpen;
}

PUBLIC STATIC bool Studio::HandleEvent(SDL_Event* event) {
    if (!Studio::Visible)
        return false;

    return UICore::HandleEvent(event);
}

// ----------------------------------------------------------------- scanning -

// Looks for anything openable next to the engine: the working directory itself,
// any subfolder holding a Resources directory, and any .hatch data file.
PRIVATE STATIC void Studio::ScanForProjects() {
    FoundProjects.clear();
    SelectedProject = -1;

    if (Directory::Exists("Resources"))
        FoundProjects.push_back(".");

    vector<char*> directories = Directory::GetDirectories(".", "*", false);
    for (size_t i = 0; i < directories.size(); i++) {
        char resourcePath[4096];
        snprintf(resourcePath, sizeof(resourcePath), "%s/Resources", directories[i]);

        if (Directory::Exists(resourcePath))
            FoundProjects.push_back(std::string(directories[i]));

        free(directories[i]);
    }

    vector<char*> dataFiles = Directory::GetFiles(".", "*.hatch", false);
    for (size_t i = 0; i < dataFiles.size(); i++) {
        FoundProjects.push_back(std::string(dataFiles[i]));
        free(dataFiles[i]);
    }

    // Anything opened before that still exists is worth offering again.
    for (size_t i = 0; i < RecentProjects.size(); i++) {
        const std::string& path = RecentProjects[i];
        if (!Directory::Exists(path.c_str()) && !File::Exists(path.c_str()))
            continue;

        bool known = false;
        for (size_t j = 0; j < FoundProjects.size(); j++) {
            if (FoundProjects[j] == path) {
                known = true;
                break;
            }
        }

        if (!known)
            FoundProjects.push_back(path);
    }

    ProjectsScanned = true;
}

#define STUDIO_MAX_RECENT 8

PRIVATE STATIC void Studio::LoadRecentProjects() {
    RecentProjects.clear();

    if (!Application::Settings)
        return;

    for (int i = 0; i < STUDIO_MAX_RECENT; i++) {
        char key[32];
        char path[4096];
        snprintf(key, sizeof(key), "recent%d", i);

        if (Application::Settings->GetString("studio", key, path, sizeof(path)))
            RecentProjects.push_back(std::string(path));
    }
}

PRIVATE STATIC void Studio::SaveRecentProjects() {
    if (!Application::Settings)
        return;

    for (int i = 0; i < STUDIO_MAX_RECENT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "recent%d", i);

        if (i < (int)RecentProjects.size())
            Application::Settings->SetString("studio", key, RecentProjects[i].c_str());
        else
            Application::Settings->RemoveProperty("studio", key);
    }

    Application::SaveSettings();
}

PRIVATE STATIC void Studio::ClearRecentProjects() {
    RecentProjects.clear();
    Studio::SaveRecentProjects();
    Studio::SetStatus("Recent projects cleared.");

    ProjectsScanned = false;
}

PRIVATE STATIC void Studio::RememberProject(const char* path) {
    // Move it to the top of the list, dropping any earlier mention of it and
    // whatever fell off the end.
    for (size_t i = 0; i < RecentProjects.size(); i++) {
        if (RecentProjects[i] == path) {
            RecentProjects.erase(RecentProjects.begin() + i);
            break;
        }
    }

    RecentProjects.insert(RecentProjects.begin(), std::string(path));

    while (RecentProjects.size() > STUDIO_MAX_RECENT)
        RecentProjects.pop_back();

    Studio::SaveRecentProjects();
}

PRIVATE STATIC void Studio::ScanForSceneFiles() {
    SceneFiles.clear();

    if (ResourceManager::UsingDataFolder && Directory::Exists("Resources/Scenes")) {
        vector<char*> files = Directory::GetFiles("Resources/Scenes", "*.*", true);
        for (size_t i = 0; i < files.size(); i++) {
            const char* path = files[i];

            // Everything the engine can open as a scene lives under
            // Resources/, and Scene::LoadScene wants the path relative to it.
            const char* relative = StringUtils::StrCaseStr(path, "Resources/");
            if (relative)
                relative += strlen("Resources/");
            else
                relative = path;

            if (StringUtils::StrCaseStr(path, ".tmx") ||
                StringUtils::StrCaseStr(path, ".bin") ||
                StringUtils::StrCaseStr(path, ".hcsn"))
                SceneFiles.push_back(std::string(relative));

            free(files[i]);
        }
    }

    SceneFilesScanned = true;
}

// --------------------------------------------------------------- new scene -

// Writes a new scene into the project and hands back the path to load it by,
// relative to Resources the way the engine wants it.
//
// It goes in a folder of its own, next to the scenes that are already there,
// because a scene's tile collision sits beside it under the same name and that
// is the layout the engine looks for.
PRIVATE STATIC bool Studio::CreateScene(const char* name) {
    if (!ResourceManager::UsingDataFolder) {
        Studio::SetStatus("Scenes can only be created in a project folder, not in a .hatch file.");
        return false;
    }

    if (!name || !*name) {
        Studio::SetStatus("Enter a name for the new scene.");
        return false;
    }

    for (const char* i = name; *i; i++) {
        if (*i == '/' || *i == '\\' || *i == ':') {
            Studio::SetStatus("Scene name cannot contain path separators.");
            return false;
        }
    }

    char folder[1024];
    snprintf(folder, sizeof(folder), "Resources/Scenes/%s", name);

    char path[1200];
    snprintf(path, sizeof(path), "%s/%s.tmx", folder, name);

    if (File::Exists(path)) {
        Studio::SetStatus("\"%s\" already exists.", path);
        return false;
    }

    if (!Directory::Exists(folder) && !Directory::CreatePath(folder)) {
        Studio::SetStatus("Could not create \"%s\".", folder);
        return false;
    }

    // The tilesets are written as paths relative to the new scene's folder, so
    // the writer needs to know where that is under Resources.
    char sceneFolder[1024];
    snprintf(sceneFolder, sizeof(sceneFolder), "Scenes/%s/", name);

    bool withTilesets = NewSceneTakeTilesets && Scene::Tilesets.size() > 0;

    if (!TiledMapWriter::WriteNew(path, sceneFolder,
            NewSceneWidth, NewSceneHeight,
            NewSceneTileWidth, NewSceneTileHeight,
            NewSceneLayers, withTilesets)) {
        Studio::SetStatus("Could not write \"%s\".", path);
        return false;
    }

    NewScenePending = std::string("Scenes/") + name + "/" + name + ".tmx";

    SceneFilesScanned = false;

    Studio::SetStatus("Created %s (%dx%d, %d layer%s%s).",
        NewScenePending.c_str(), NewSceneWidth, NewSceneHeight, NewSceneLayers,
        NewSceneLayers == 1 ? "" : "s",
        withTilesets ? ", with this scene's tilesets" : "");

    return true;
}

// ------------------------------------------------------------- new project -

// Writes out the smallest tree the engine will boot from, so a new project can
// be created and opened without touching a shell.
PRIVATE STATIC bool Studio::CreateProject(const char* name) {
    if (!name || !*name) {
        Studio::SetStatus("Enter a folder name for the new project.");
        return false;
    }

    for (const char* i = name; *i; i++) {
        if (*i == '/' || *i == '\\' || *i == ':') {
            Studio::SetStatus("Project name cannot contain path separators.");
            return false;
        }
    }

    if (Directory::Exists(name)) {
        Studio::SetStatus("\"%s\" already exists.", name);
        return false;
    }

    static const char* folders[] = {
        "Resources",
        "Resources/Game",
        "Resources/Scenes",
        "Resources/Scripts",
        "Resources/Sprites",
        "Resources/Images",
        "Resources/Sounds",
        "Resources/Music",
    };

    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", name, folders[i]);

        if (!Directory::CreatePath(path)) {
            Studio::SetStatus("Could not create \"%s\".", path);
            return false;
        }
    }

    char gameConfig[2048];
    snprintf(gameConfig, sizeof(gameConfig),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<game>\n"
        "    <name>%s</name>\n"
        "    <shortTitle>%s</shortTitle>\n"
        "    <version>1.0.0</version>\n"
        "    <description>Made with the Hatch Game Engine.</description>\n"
        "\n"
        "    <engine>\n"
        "        <loadAllClasses>false</loadAllClasses>\n"
        "        <useSoftwareRenderer>false</useSoftwareRenderer>\n"
        "    </engine>\n"
        "\n"
        "    <display>\n"
        "        <width>848</width>\n"
        "        <height>480</height>\n"
        "    </display>\n"
        "\n"
        "    <audio volume=\"100\">\n"
        "        <music volume=\"100\"/>\n"
        "        <sound volume=\"100\"/>\n"
        "    </audio>\n"
        "</game>\n", name, name);

    static const char* sceneConfig =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<sceneconfig>\n"
        "    <category name=\"Main\">\n"
        "        <!-- Add scenes here, for example:\n"
        "        <stage name=\"Test\" folder=\"Test\" id=\"1\" fileExtension=\"tmx\"/>\n"
        "        -->\n"
        "    </category>\n"
        "</sceneconfig>\n";

    struct {
        const char* Path;
        const char* Contents;
    } files[] = {
        { "Resources/GameConfig.xml", gameConfig },
        { "Resources/Game/SceneConfig.xml", sceneConfig },
    };

    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", name, files[i].Path);

        if (!File::WriteAllBytes(path, files[i].Contents, strlen(files[i].Contents))) {
            Studio::SetStatus("Could not write \"%s\".", path);
            return false;
        }
    }

    Studio::SetStatus("Created project \"%s\".", name);
    Log::Print(Log::LOG_IMPORTANT, "Created new project in \"%s\".", name);

    ProjectsScanned = false;

    return true;
}

PRIVATE STATIC void Studio::QueueAction(int action) {
    PendingAction = action;
}

PRIVATE STATIC void Studio::OpenProject(const char* path) {
    // Unsaved scene edits would go with the old project, so ask first.
    if (!ResourceEditor::RequestAction(ResourceEditor::PENDING_OPEN_PROJECT, path))
        return;

    StringUtils::Copy(PendingPath, path, sizeof(PendingPath));
    Studio::QueueAction(STUDIO_ACTION_OPEN_PROJECT);
    Studio::SetStatus("Opening \"%s\"...", path);
}

// Carries out whatever the last frame's buttons asked for. Called from the
// frame loop, outside of any UI drawing.
PUBLIC STATIC void Studio::Update() {
    int action = PendingAction;
    PendingAction = STUDIO_ACTION_NONE;

    switch (action) {
        case STUDIO_ACTION_OPEN_PROJECT:
            if (Application::LoadProject(PendingPath)) {
                Studio::RememberProject(PendingPath);
                Studio::SetStatus("Opened \"%s\".", PendingPath);

                SceneFilesScanned = false;
                ProjectsScanned = false;
                SelectedScene = -1;

                SceneEditor::Reset();
                ResourceEditor::Invalidate();
                TileCollisionEditor::Invalidate();
            }
            else
                Studio::SetStatus("Could not open \"%s\".", PendingPath);
            break;

        case STUDIO_ACTION_RESTART_ENGINE:
            Application::RestartApplication();
            Studio::SetStatus("Engine restarted.");
            break;

        case STUDIO_ACTION_RELOAD_SCENE:
            Application::ReloadScene();
            Studio::SetStatus("Scripts recompiled and scene reloaded.");
            break;

        case STUDIO_ACTION_RESTART_SCENE:
            Application::RestartScene();
            Studio::SetStatus("Scene restarted.");
            break;

        case STUDIO_ACTION_SET_WINDOW_SIZE:
            Application::WindowWidth = PendingValueA;
            Application::WindowHeight = PendingValueB;
            Application::SetWindowSize(PendingValueA, PendingValueB);
            Studio::SetStatus("Window resized to %d x %d.", PendingValueA, PendingValueB);
            break;

        case STUDIO_ACTION_SET_FULLSCREEN:
            Application::SetWindowFullscreen(PendingValueA != 0);
            break;

        case STUDIO_ACTION_CLOSE_PROJECT:
            Application::CloseProject();
            Studio::SetStatus("Project closed.");

            ProjectsScanned = false;
            SceneFilesScanned = false;
            SelectedScene = -1;

            SceneEditor::Reset();
            ResourceEditor::Invalidate();
            TileCollisionEditor::Invalidate();
            break;

        case STUDIO_ACTION_RUN:
            Studio::Run();
            break;
    }
}

// ----------------------------------------------------------------- commands -

PRIVATE STATIC void Studio::BrowseFor(int what) {
    char startPath[4096];
    Application::GetProjectPath(startPath, sizeof(startPath));

    BrowsingFor = what;

    switch (what) {
        case STUDIO_BROWSE_PROJECT_FOLDER:
            UIFileDialog::Open("Open Project Folder", UIFileDialog::PICK_FOLDER, startPath, "*");
            break;
        case STUDIO_BROWSE_DATA_FILE:
            UIFileDialog::Open("Open Data File", UIFileDialog::PICK_FILE, startPath, "*.hatch");
            break;
        case STUDIO_BROWSE_SCENE_FILE:
            UIFileDialog::Open("Open Scene File", UIFileDialog::PICK_FILE, startPath, "*.tmx");
            break;
        case STUDIO_BROWSE_SCRIPTS_FOLDER:
            UIFileDialog::Open("Choose Scripts Folder", UIFileDialog::PICK_FOLDER, startPath, "*");
            break;
    }
}

// Called once the browser closes with a path.
PRIVATE STATIC void Studio::FinishBrowse(const char* path) {
    switch (BrowsingFor) {
        case STUDIO_BROWSE_PROJECT_FOLDER:
        case STUDIO_BROWSE_DATA_FILE:
            Studio::OpenProject(path);
            break;

        case STUDIO_BROWSE_SCENE_FILE: {
            // Scene paths are given relative to the Resources folder.
            const char* relative = StringUtils::StrCaseStr(path, "Resources/");
            if (relative) {
                Application::QueueSceneChange(relative + strlen("Resources/"));
                Studio::SetStatus("Loading \"%s\"...", relative + strlen("Resources/"));
            }
            else
                Studio::SetStatus("Scenes have to live inside the project's Resources folder.");
            break;
        }

        case STUDIO_BROWSE_SCRIPTS_FOLDER:
            StringUtils::Copy(ScriptsFolderText, path, sizeof(ScriptsFolderText));
            StringUtils::Copy(SourceFileMap::Path, path, sizeof(SourceFileMap::Path));
            Studio::SetStatus("Scripts folder set to \"%s\".", path);
            break;
    }

    BrowsingFor = STUDIO_BROWSE_NONE;
}

// Closes the editor and lets the game play, from either the start scene or
// whatever is already loaded.
PRIVATE STATIC void Studio::Run() {
    if (RunFromStartScene) {
        const char* startScene = Application::GetStartingScene();
        if (startScene && *startScene)
            Application::QueueSceneChange(startScene);
    }

    Studio::Hide();
}

PRIVATE STATIC void Studio::SetRunFromStartScene(bool fromStart) {
    RunFromStartScene = fromStart;

    if (Application::Settings)
        Application::Settings->SetBool("studio", "runFromStartScene", fromStart);
}

// The menu bar, in the shape HatchStudio uses. Drawn after the panels so its
// dropdowns sit over them.
PRIVATE STATIC void Studio::DrawMenuBar(float width, float height) {
    UIMenu::Begin(0.0f, 0.0f, width, height);

    if (UIMenu::BeginMenu("File")) {
        if (UIMenu::Item("New Project...", "Ctrl+Alt+N", true))
            Studio::NewProjectCommand();

        if (UIMenu::Item("New Scene...", "Ctrl+N", true))
            Studio::NewSceneCommand();

        if (UIMenu::Item("Open Project...", "Ctrl+Alt+O", true))
            Studio::BrowseFor(STUDIO_BROWSE_PROJECT_FOLDER);

        if (UIMenu::Item("Open Data File...", "Ctrl+O", true))
            Studio::BrowseFor(STUDIO_BROWSE_DATA_FILE);

        if (UIMenu::BeginSubmenu("Recent Projects")) {
            for (size_t i = 0; i < RecentProjects.size(); i++) {
                if (UIMenu::Item(RecentProjects[i].c_str(), NULL, true))
                    Studio::OpenProject(RecentProjects[i].c_str());
            }

            if (!RecentProjects.size())
                UIMenu::Item("(none)", NULL, false);
            else {
                UIMenu::Separator();
                if (UIMenu::Item("Clear Recent Projects", NULL, true))
                    Studio::ClearRecentProjects();
            }

            UIMenu::EndSubmenu();
        }

        if (UIMenu::Item("Close Project", "Ctrl+Alt+W", true)) {
            if (ResourceEditor::RequestAction(ResourceEditor::PENDING_CLOSE_PROJECT, NULL))
                Studio::QueueAction(STUDIO_ACTION_CLOSE_PROJECT);
        }

        UIMenu::Separator();

        if (UIMenu::Item("Save Settings", "Ctrl+S", true))
            Studio::SaveSettingsCommand();

        UIMenu::Separator();

        if (UIMenu::Item("Exit", NULL, true))
            Application::Running = false;

        UIMenu::EndMenu();
    }

    if (UIMenu::BeginMenu("Project")) {
        if (UIMenu::Item("Run", "Ctrl+R", true))
            Studio::QueueAction(STUDIO_ACTION_RUN);

        if (UIMenu::Item("Restart Scene", "F6", true))
            Studio::QueueAction(STUDIO_ACTION_RESTART_SCENE);

        if (UIMenu::Item("Recompile Scripts & Reload Scene", "F5", true))
            Studio::QueueAction(STUDIO_ACTION_RELOAD_SCENE);

        if (UIMenu::Item("Restart Engine", "F1", true))
            Studio::QueueAction(STUDIO_ACTION_RESTART_ENGINE);

        UIMenu::Separator();

        if (UIMenu::BeginSubmenu("Set Run Start Scene")) {
            if (UIMenu::RadioItem("Run From Start Scene", RunFromStartScene, true))
                Studio::SetRunFromStartScene(true);

            if (UIMenu::RadioItem("Run From Current Scene", !RunFromStartScene, true))
                Studio::SetRunFromStartScene(false);

            UIMenu::EndSubmenu();
        }

        UIMenu::Separator();

        if (UIMenu::Item("Open Scene File...", NULL, true))
            Studio::BrowseFor(STUDIO_BROWSE_SCENE_FILE);

        if (UIMenu::Item("Rescan Project", NULL, true)) {
            ProjectsScanned = false;
            SceneFilesScanned = false;
            Studio::SetStatus("Rescanned.");
        }

        UIMenu::EndMenu();
    }

    if (UIMenu::BeginMenu("View")) {
        for (int i = 0; i < STUDIO_TAB_COUNT; i++) {
            if (UIMenu::RadioItem(TabNames[i], CurrentTab == i, true))
                CurrentTab = i;
        }

        UIMenu::Separator();

        if (UIMenu::CheckItem("Pause Game While Editing", NULL, Studio::PauseGameWhileOpen, true)) {
            Studio::PauseGameWhileOpen = !Studio::PauseGameWhileOpen;
            if (Application::Settings)
                Application::Settings->SetBool("studio", "pauseWhenOpen", Studio::PauseGameWhileOpen);
        }

        if (UIMenu::CheckItem("Performance Overlay", NULL, Application::IsShowingPerformance(), true))
            Application::SetShowingPerformance(!Application::IsShowingPerformance());

        if (UIMenu::CheckItem("Fullscreen", "F4", Application::GetWindowFullscreen(), true)) {
            PendingValueA = Application::GetWindowFullscreen() ? 0 : 1;
            Studio::QueueAction(STUDIO_ACTION_SET_FULLSCREEN);
        }

        UIMenu::EndMenu();
    }

    if (UIMenu::BeginMenu("Help")) {
        if (UIMenu::Item("Controls & About", NULL, true))
            CurrentTab = STUDIO_TAB_HELP;

        UIMenu::Separator();

        if (UIMenu::Item("Close Editor", "F12", true))
            Studio::Hide();

        UIMenu::EndMenu();
    }

    UIMenu::End();
}

PRIVATE STATIC void Studio::NewProjectCommand() {
    CurrentTab = STUDIO_TAB_PROJECT;
    Studio::SetStatus("Name the project under New Project, then press Create.");
}

PRIVATE STATIC void Studio::NewSceneCommand() {
    CurrentTab = STUDIO_TAB_SCENES;

    // A new scene takes its tile size from the one that is loaded, since it is
    // nearly always another level for the same game.
    if (Scene::TileWidth > 0 && Scene::TileHeight > 0) {
        NewSceneTileWidth = Scene::TileWidth;
        NewSceneTileHeight = Scene::TileHeight;
    }

    Studio::SetStatus("Name the scene under New Scene, then press Create Scene.");
}

PRIVATE STATIC void Studio::SaveSettingsCommand() {
    Application::SaveSettings();
    Studio::SetStatus("Saved %s.", Application::SettingsFile);
}

// Menu shortcuts. Handled here rather than in the key handler so that the menu
// and its accelerator cannot drift apart.
PRIVATE STATIC void Studio::HandleShortcuts() {
    const Uint16 ctrl = KMOD_CTRL;
    const Uint16 ctrlAlt = KMOD_CTRL | KMOD_ALT;

    if (UICore::WasShortcutPressed(SDLK_n, ctrl))
        Studio::NewSceneCommand();
    else if (UICore::WasShortcutPressed(SDLK_n, ctrlAlt))
        Studio::NewProjectCommand();
    else if (UICore::WasShortcutPressed(SDLK_o, ctrlAlt))
        Studio::BrowseFor(STUDIO_BROWSE_PROJECT_FOLDER);
    else if (UICore::WasShortcutPressed(SDLK_o, ctrl))
        Studio::BrowseFor(STUDIO_BROWSE_DATA_FILE);
    else if (UICore::WasShortcutPressed(SDLK_w, ctrlAlt))
        Studio::QueueAction(STUDIO_ACTION_CLOSE_PROJECT);
    else if (UICore::WasShortcutPressed(SDLK_s, ctrl))
        Studio::SaveSettingsCommand();
    else if (UICore::WasShortcutPressed(SDLK_r, ctrl))
        Studio::QueueAction(STUDIO_ACTION_RUN);
}

// ------------------------------------------------------------------- tabs --

PRIVATE STATIC void Studio::DrawProjectTab(float x, float y, float w, float h, bool split) {
    float halfW = split ? w / 2.0f : w;
    float halfH = split ? h : h / 2.0f;
    float secondX = split ? x + halfW : x;
    float secondY = split ? y : y + halfH;

    UICore::BeginPanel("Projects", x, y, halfW, halfH);
        if (!ProjectsScanned)
            Studio::ScanForProjects();

        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
        if (UICore::Button("Rescan"))
            ProjectsScanned = false;

        UICore::SameLine();
        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
        if (UICore::ButtonEnabled("Open Project",
                SelectedProject >= 0 && SelectedProject < (int)FoundProjects.size()))
            Studio::OpenProject(FoundProjects[SelectedProject].c_str());

        UICore::Separator();

        if (!FoundProjects.size())
            UICore::Text("No projects found next to the engine.", UI_COL_TEXT_FAINT);

        UICore::ResetRowStriping();
        for (size_t i = 0; i < FoundProjects.size(); i++) {
            char label[4200];
            snprintf(label, sizeof(label), "%s##project%d", FoundProjects[i].c_str(), (int)i);

            if (UICore::ListItem(label, SelectedProject == (int)i))
                SelectedProject = (int)i;
        }
    UICore::EndPanel();

    UICore::BeginPanel("Current Project", secondX, secondY, halfW, halfH);
        char projectPath[4096];
        Application::GetProjectPath(projectPath, sizeof(projectPath));

        UICore::Field("Title", Application::GameTitle);
        UICore::Field("Version", Application::GameVersion);
        UICore::Field("Description", Application::GameDescription);
        UICore::Field("Folder", projectPath);
        UICore::Field("Reading from", ResourceManager::UsingDataFolder ?
            "Resources folder" : "packed .hatch file");
        UICore::FieldFormatted("Scenes listed", "%d", (int)SceneInfo::Entries.size());

        UICore::Separator();
        UICore::Heading("New Project");
        UICore::Text("Creates a folder with the Resources tree and a", UI_COL_TEXT_DIM);
        UICore::Text("starter GameConfig.xml, ready to open.", UI_COL_TEXT_DIM);

        UICore::TextField("Name", NewProjectName, sizeof(NewProjectName));

        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
        if (UICore::Button("Create"))
            Studio::CreateProject(NewProjectName);

        UICore::SameLine();
        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
        if (UICore::Button("Create & Open")) {
            if (Studio::CreateProject(NewProjectName))
                Studio::OpenProject(NewProjectName);
        }
    UICore::EndPanel();
}

PRIVATE STATIC void Studio::DrawHierarchyTab(float x, float y, float w, float h, bool split) {
    float halfW = split ? w / 2.0f : w;
    float halfH = split ? h : h / 2.0f;
    float secondX = split ? x + halfW : x;
    float secondY = split ? y : y + halfH;

    UICore::BeginPanel("Hierarchy", x, y, halfW, halfH);
        Hierarchy::Draw();
    UICore::EndPanel();

    char title[128];
    char name[96];
    Selection::GetName(name, sizeof(name));

    // The panel says what it is looking at, so the two halves read as one
    // thing rather than as a list beside an unrelated form.
    if (name[0])
        snprintf(title, sizeof(title), "Inspector - %s", name);
    else
        snprintf(title, sizeof(title), "Inspector");

    UICore::BeginPanel(title, secondX, secondY, halfW, halfH);
        Inspector::Draw();
    UICore::EndPanel();
}

PRIVATE STATIC void Studio::DrawScenesTab(float x, float y, float w, float h, bool split) {
    float halfW = split ? w / 2.0f : w;
    float halfH = split ? h : h / 2.0f;
    float secondX = split ? x + halfW : x;
    float secondY = split ? y : y + halfH;

    if (!SceneFilesScanned)
        Studio::ScanForSceneFiles();

    UICore::BeginPanel("Scene List", x, y, halfW, halfH);
        if (SceneInfo::Entries.size()) {
            for (size_t c = 0; c < SceneInfo::Categories.size(); c++) {
                SceneListCategory& category = SceneInfo::Categories[c];

                UICore::Heading(category.Name ? category.Name : "Unnamed");
                UICore::ResetRowStriping();

                for (size_t e = category.OffsetStart; e < category.OffsetEnd; e++) {
                    SceneListEntry& entry = SceneInfo::Entries[e];

                    char label[512];
                    snprintf(label, sizeof(label), "%s##entry%d",
                        entry.Name ? entry.Name : "Unnamed", (int)e);

                    if (UICore::ListItem(label, SelectedScene == (int)e))
                        SelectedScene = (int)e;
                }
            }
        }
        else {
            UICore::Text("This project has no scene list.", UI_COL_TEXT_DIM);
            UICore::Text("Add scenes to Resources/Game/SceneConfig.xml", UI_COL_TEXT_FAINT);
            UICore::Text("to have them show up here.", UI_COL_TEXT_FAINT);
        }

        if (SceneFiles.size()) {
            UICore::Separator();
            UICore::Heading("Scene Files On Disk");
            UICore::ResetRowStriping();

            for (size_t i = 0; i < SceneFiles.size(); i++) {
                char label[4200];
                snprintf(label, sizeof(label), "%s##file%d", SceneFiles[i].c_str(), (int)i);

                if (UICore::ListItem(label, false)) {
                    Application::QueueSceneChange(SceneFiles[i].c_str());
                    Studio::SetStatus("Loading \"%s\"...", SceneFiles[i].c_str());
                }
            }
        }
    UICore::EndPanel();

    UICore::BeginPanel("Selected Scene", secondX, secondY, halfW, halfH);
        bool haveSelection = SceneInfo::IsEntryValid(SelectedScene);
        std::string filename;

        if (haveSelection) {
            SceneListEntry& entry = SceneInfo::Entries[SelectedScene];
            filename = SceneInfo::GetFilename(SelectedScene);

            UICore::Field("Name", entry.Name ? entry.Name : "Unnamed");
            UICore::Field("ID", entry.ID ? entry.ID : "-");
            UICore::Field("Folder", entry.Folder ? entry.Folder : "-");
            UICore::Field("Type", entry.Filetype ? entry.Filetype : "-");
            UICore::Field("Path", filename.c_str());
        }
        else
            UICore::Text("Pick a scene from the list.", UI_COL_TEXT_FAINT);

        UICore::Separator();

        if (UICore::ButtonEnabled("Load Selected Scene", haveSelection)) {
            Application::QueueSceneChange(filename.c_str());
            Studio::SetStatus("Loading \"%s\"...", filename.c_str());
        }

        UICore::Separator();
        UICore::Heading("Running Scene");
        UICore::Field("Current", Scene::CurrentScene[0] ? Scene::CurrentScene : "(none)");

        if (UICore::Button("Restart Scene"))
            Studio::QueueAction(STUDIO_ACTION_RESTART_SCENE);

        if (UICore::Button("Reload Scripts & Scene"))
            Studio::QueueAction(STUDIO_ACTION_RELOAD_SCENE);

        if (UICore::Button("Rescan Scene Files"))
            SceneFilesScanned = false;

        UICore::Separator();
        UICore::Heading("Export For SEGA");

        if (!Scene::CurrentScene[0])
            UICore::Text("Load a scene to export it.", UI_COL_TEXT_FAINT);
        else {
            UICore::TextField("Folder", SegaExportPath, sizeof(SegaExportPath));

            // Genesis is not offered separately: it is the Mega Drive under its
            // North American name, the same machine down to the VDP.
            if (UICore::Button("Mega Drive / Genesis")) {
                MegaDriveExportResult exported = MegaDriveExporter::ExportScene(SegaExportPath);

                Studio::SetStatus("%s", exported.Message);
                Log::Print(exported.Success ? Log::LOG_INFO : Log::LOG_ERROR, "%s", exported.Message);
            }

            if (UICore::Button("32X")) {
                Sega32XExportResult exported = Sega32XExporter::ExportScene(SegaExportPath);

                Studio::SetStatus("%s", exported.Message);
                Log::Print(exported.Success ? Log::LOG_INFO : Log::LOG_ERROR, "%s", exported.Message);
            }

            if (UICore::Button("Mega CD")) {
                MegaCDExportResult exported = MegaCDExporter::ExportScene(SegaExportPath);

                Studio::SetStatus("%s", exported.Message);
                Log::Print(exported.Success ? Log::LOG_INFO : Log::LOG_ERROR, "%s", exported.Message);
            }

            if (UICore::Button("Game Gear")) {
                GameGearExportResult exported = GameGearExporter::ExportScene(SegaExportPath);

                Studio::SetStatus("%s", exported.Message);
                Log::Print(exported.Success ? Log::LOG_INFO : Log::LOG_ERROR, "%s", exported.Message);
            }

            if (UICore::Button("Saturn")) {
                SegaSaturnExportResult exported = SegaSaturnExporter::ExportScene(SegaExportPath);

                Studio::SetStatus("%s", exported.Message);
                Log::Print(exported.Success ? Log::LOG_INFO : Log::LOG_ERROR, "%s", exported.Message);
            }

            UICore::Text("Writes a project that builds into a cartridge or a", UI_COL_TEXT_FAINT);
            UICore::Text("disc. The Mega Drive and Mega CD get the scene as", UI_COL_TEXT_FAINT);
            UICore::Text("tiles for their VDP; the 32X and the Saturn have", UI_COL_TEXT_FAINT);
            UICore::Text("framebuffers, so they get it drawn, with far more", UI_COL_TEXT_FAINT);
            UICore::Text("colour. Game logic does not come across to any of", UI_COL_TEXT_FAINT);
            UICore::Text("them. The 3D tab exports a 3D scene to the Saturn.", UI_COL_TEXT_FAINT);
        }

        UICore::Separator();
        UICore::Heading("New Scene");

        if (!ResourceManager::UsingDataFolder)
            UICore::Text("Scenes cannot be added to a .hatch file.", UI_COL_TEXT_FAINT);
        else {
            UICore::TextField("Name", NewSceneName, sizeof(NewSceneName));
            UICore::SliderInt("Width in tiles", &NewSceneWidth, 1, 256);
            UICore::SliderInt("Height in tiles", &NewSceneHeight, 1, 256);
            UICore::SliderInt("Tile width", &NewSceneTileWidth, 8, 128);
            UICore::SliderInt("Tile height", &NewSceneTileHeight, 8, 128);
            UICore::SliderInt("Layers", &NewSceneLayers, 1, 4);

            // A new level is nearly always another level for the game being
            // worked on, so the scene that is loaded lends its tilesets. It is
            // also the only way to know a tileset's size without going and
            // reading the image.
            if (Scene::Tilesets.size()) {
                char label[128];
                snprintf(label, sizeof(label), "Use this scene's tileset%s",
                    Scene::Tilesets.size() == 1 ? "" : "s");

                UICore::Checkbox(label, &NewSceneTakeTilesets);
            }
            else
                UICore::Text("Load a scene first to give this one its tilesets.", UI_COL_TEXT_FAINT);

            UICore::Checkbox("Open it once created", &NewSceneOpen);

            if (UICore::Button("Create Scene")) {
                if (Studio::CreateScene(NewSceneName) && NewSceneOpen) {
                    Application::QueueSceneChange(NewScenePending.c_str());
                    Studio::SetStatus("Created and loading \"%s\"...", NewScenePending.c_str());
                }
            }
        }
    UICore::EndPanel();
}

PRIVATE STATIC void Studio::DrawPlayTab(float x, float y, float w, float h, bool split) {
    float halfW = split ? w / 2.0f : w;
    float halfH = split ? h : h / 2.0f;
    float secondX = split ? x + halfW : x;
    float secondY = split ? y : y + halfH;

    UICore::BeginPanel("Playback", x, y, halfW, halfH);
        UICore::Checkbox("Pause the game while this editor is open", &Studio::PauseGameWhileOpen);
        UICore::Checkbox("Scene paused", &Scene::Paused);
        UICore::Checkbox("Frame stepper", &Application::Stepper);

        if (UICore::ButtonEnabled("Step One Frame", Application::Stepper)) {
            Application::Step = true;
            Studio::SetStatus("Stepped one frame.");
        }

        bool fastForward = Application::IsFastForwarding();
        if (UICore::Checkbox("Fast forward", &fastForward))
            Application::SetFastForwarding(fastForward);

        UICore::Separator();
        UICore::Heading("Reload");

        if (UICore::Button("Restart Scene"))
            Studio::QueueAction(STUDIO_ACTION_RESTART_SCENE);

        if (UICore::Button("Recompile Scripts & Reload Scene"))
            Studio::QueueAction(STUDIO_ACTION_RELOAD_SCENE);

        if (UICore::Button("Restart Engine"))
            Studio::QueueAction(STUDIO_ACTION_RESTART_ENGINE);

        UICore::Separator();
        UICore::Heading("Debug View");

        bool showHitboxes = Scene::ShowHitboxes;
        if (UICore::Checkbox("Show hitboxes", &showHitboxes))
            Scene::ShowHitboxes = showHitboxes;

        bool showRegions = Scene::ShowObjectRegions != 0;
        if (UICore::Checkbox("Show object regions", &showRegions))
            Scene::ShowObjectRegions = showRegions ? 1 : 0;

        bool showPerformance = Application::IsShowingPerformance();
        if (UICore::Checkbox("Show performance overlay", &showPerformance))
            Application::SetShowingPerformance(showPerformance);

        // The overlay draws with a font out of the game's own resources, and
        // most projects do not have one. The breakdown beside this is drawn
        // with the editor's font, so it is there either way.
        if (showPerformance && !Application::HasPerformanceFont())
            UICore::Text("Needs Debug/Font.png in this game -- see Frame Time.");

        static const char* collisionModes[] = { "off", "path A", "path B" };
        char collisionLabel[64];
        snprintf(collisionLabel, sizeof(collisionLabel), "Tile collision: %s",
            collisionModes[Scene::ShowTileCollisionFlag % 3]);

        if (UICore::Button(collisionLabel)) {
            Scene::ShowTileCollisionFlag = (Scene::ShowTileCollisionFlag + 1) % 3;
            Application::UpdateWindowTitle();
        }

        if (UICore::Button("Write Performance Snapshot To Log")) {
            Application::TakePerformanceSnapshot();
            Studio::SetStatus("Snapshot written to the console.");
        }
    UICore::EndPanel();

    UICore::BeginPanel("Statistics", secondX, secondY, halfW, halfH);
        UICore::FieldFormatted("FPS", "%.1f", Application::FPS);
        UICore::Field("Renderer", Graphics::Renderer ? Graphics::Renderer : "unknown");
        UICore::Field("Scene", Scene::CurrentScene[0] ? Scene::CurrentScene : "(none)");
        UICore::FieldFormatted("Frame", "%d", Scene::Frame);
        UICore::FieldFormatted("Objects", "%d", Scene::ObjectCount);
        UICore::FieldFormatted("Static objects", "%d", Scene::StaticObjectCount);
        UICore::FieldFormatted("Dynamic objects", "%d", Scene::DynamicObjectCount);
        UICore::FieldFormatted("Tilesets", "%d", (int)Scene::Tilesets.size());
        UICore::FieldFormatted("Tile size", "%d x %d", Scene::TileWidth, Scene::TileHeight);
        UICore::FieldFormatted("Active views", "%d", Scene::ViewsActive);

        UICore::Separator();
        UICore::Heading("Frame Time");

        // What the performance overlay breaks a frame down into, drawn here
        // with the editor's own font so it works in a game that has no font of
        // its own to lend it.
        double total = 0.0;
        for (int i = 0; i < Application::GetFrameMetricCount(); i++) {
            double value = Application::GetFrameMetric(i);
            total += value;

            UICore::FieldFormatted(Application::GetFrameMetricName(i), "%.3f ms", value);
        }

        UICore::FieldFormatted("Total", "%.3f ms", total);

        UICore::Separator();
        UICore::Heading("Layers");

        if (!Scene::Layers.size())
            UICore::Text("No layers in this scene.", UI_COL_TEXT_FAINT);

        for (size_t i = 0; i < Scene::Layers.size(); i++) {
            SceneLayer& layer = Scene::Layers[i];

            char label[512];
            snprintf(label, sizeof(label), "%s  (%dx%d)##layer%d",
                layer.Name, layer.Width, layer.Height, (int)i);

            bool visible = layer.Visible;
            if (UICore::Checkbox(label, &visible))
                layer.Visible = visible;
        }
    UICore::EndPanel();
}

PRIVATE STATIC void Studio::DrawSettingsTab(float x, float y, float w, float h, bool split) {
    float halfW = split ? w / 2.0f : w;
    float halfH = split ? h : h / 2.0f;
    float secondX = split ? x + halfW : x;
    float secondY = split ? y : y + halfH;

    INI* settings = Application::Settings;

    UICore::BeginPanel("Display & Audio", x, y, halfW, halfH);
        bool fullscreen = PendingAction == STUDIO_ACTION_SET_FULLSCREEN ?
            PendingValueA != 0 : Application::GetWindowFullscreen();
        if (UICore::Checkbox("Fullscreen", &fullscreen)) {
            PendingValueA = fullscreen ? 1 : 0;
            Studio::QueueAction(STUDIO_ACTION_SET_FULLSCREEN);
            if (settings)
                settings->SetBool("display", "fullscreen", fullscreen);
        }

        bool vsync = Graphics::VsyncEnabled;
        if (UICore::Checkbox("V-Sync", &vsync)) {
            Graphics::VsyncEnabled = vsync;
            Graphics::SetVSync(vsync);
            if (settings)
                settings->SetBool("display", "vsync", vsync);
        }

        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
        UICore::TextField("Width", WindowWidthText, sizeof(WindowWidthText));
        UICore::SameLine();
        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
        UICore::TextField("Height", WindowHeightText, sizeof(WindowHeightText));

        if (UICore::Button("Apply Window Size")) {
            int width = 0, height = 0;
            if (StringUtils::ToNumber(&width, WindowWidthText) &&
                StringUtils::ToNumber(&height, WindowHeightText) &&
                width >= 160 && height >= 120) {
                PendingValueA = width;
                PendingValueB = height;
                Studio::QueueAction(STUDIO_ACTION_SET_WINDOW_SIZE);
            }
            else
                Studio::SetStatus("Enter a window size of at least 160 x 120.");
        }

        UICore::Separator();
        UICore::Heading("Audio");

        int masterVolume = Application::MasterVolume;
        if (UICore::SliderInt("Master", &masterVolume, 0, 100)) {
            Application::SetMasterVolume(masterVolume);
            if (settings)
                settings->SetInteger("audio", "masterVolume", masterVolume);
        }

        int musicVolume = Application::MusicVolume;
        if (UICore::SliderInt("Music", &musicVolume, 0, 100)) {
            Application::SetMusicVolume(musicVolume);
            if (settings)
                settings->SetInteger("audio", "musicVolume", musicVolume);
        }

        int soundVolume = Application::SoundVolume;
        if (UICore::SliderInt("Sound", &soundVolume, 0, 100)) {
            Application::SetSoundVolume(soundVolume);
            if (settings)
                settings->SetInteger("audio", "soundVolume", soundVolume);
        }
    UICore::EndPanel();

    UICore::BeginPanel("Developer", secondX, secondY, halfW, halfH);
        bool devMenu = Application::IsDevMenuEnabled();
        if (UICore::Checkbox("Developer hotkeys (F1-F10)", &devMenu)) {
            Application::SetDevMenuEnabled(devMenu);
            if (settings)
                settings->SetBool("dev", "devMenu", devMenu);
        }

        bool openOnStart = false;
        if (settings)
            settings->GetBool("studio", "openOnStart", &openOnStart);
        if (UICore::Checkbox("Open this editor on startup", &openOnStart) && settings)
            settings->SetBool("studio", "openOnStart", openOnStart);

        if (UICore::Checkbox("Pause the game while the editor is open", &Studio::PauseGameWhileOpen) && settings)
            settings->SetBool("studio", "pauseWhenOpen", Studio::PauseGameWhileOpen);

        bool writeLog = Log::WriteToFile;
        if (UICore::Checkbox("Write the log to a file", &writeLog)) {
            Log::WriteToFile = writeLog;
            if (settings)
                settings->SetBool("dev", "writeToFile", writeLog);
        }

        int logLevel = Log::LogLevel;
        if (UICore::SliderInt("Log level", &logLevel, -1, 3)) {
            Log::SetLogLevel(logLevel);
            if (settings)
                settings->SetInteger("dev", "logLevel", logLevel);
        }
        UICore::Text("-1 verbose, 0 info, 1 warnings, 2 errors, 3 important", UI_COL_TEXT_FAINT);

        UICore::Separator();
        UICore::Heading("Renderer");

        char currentRenderer[64] = "opengl";
        if (settings)
            settings->GetString("dev", "renderer", currentRenderer, sizeof(currentRenderer));

        int rendererIndex = 0;
        for (int i = 0; i < RendererCount; i++) {
            if (!strcmp(RendererNames[i], currentRenderer)) {
                rendererIndex = i;
                break;
            }
        }

        if (UICore::Dropdown("Preferred", RendererNames, RendererCount, &rendererIndex) && settings) {
            settings->SetString("dev", "renderer", RendererNames[rendererIndex]);
            Studio::SetStatus("Renderer set to %s; restart to apply.", RendererNames[rendererIndex]);
        }
        UICore::Tooltip("Takes effect the next time the engine starts.");

        UICore::Field("In use now", Graphics::Renderer ? Graphics::Renderer : "unknown");

        UICore::Separator();
        UICore::Heading("Scripts");

        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.72f);
        if (UICore::TextField("Folder", ScriptsFolderText, sizeof(ScriptsFolderText)))
            StringUtils::Copy(SourceFileMap::Path, ScriptsFolderText, sizeof(SourceFileMap::Path));
        UICore::Tooltip("Where .hsl sources are compiled from.");

        UICore::SameLine();
        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.26f);
        if (UICore::Button("Browse..."))
            Studio::BrowseFor(STUDIO_BROWSE_SCRIPTS_FOLDER);

        UICore::Separator();

        if (UICore::Button("Save Settings"))
            Studio::SaveSettingsCommand();
        UICore::Tooltip("Writes everything on this tab to config.ini.");
    UICore::EndPanel();
}

PRIVATE STATIC void Studio::DrawConsoleTab(float x, float y, float w, float h) {
    float toolbarHeight = UICore::RowHeight() + UICore::Pad() * 3.0f;
    if (toolbarHeight > h)
        toolbarHeight = h;

    UICore::BeginPanel("##consoletoolbar", x, y, w, toolbarHeight);
        float buttonWidth = UICore::ContentWidth() / 5.0f - UICore::Pad();

        UICore::SetNextItemWidth(buttonWidth);
        if (UICore::Button("Clear")) {
            Log::ClearHistory();
            LastLogCount = 0;
        }

        UICore::SameLine();
        UICore::SetNextItemWidth(buttonWidth);
        UICore::Checkbox("Verbose", &ShowVerbose);
        UICore::SameLine();
        UICore::SetNextItemWidth(buttonWidth);
        UICore::Checkbox("Info", &ShowInfo);
        UICore::SameLine();
        UICore::SetNextItemWidth(buttonWidth);
        UICore::Checkbox("Warnings", &ShowWarnings);
        UICore::SameLine();
        UICore::SetNextItemWidth(buttonWidth);
        UICore::Checkbox("Follow", &FollowLog);
    UICore::EndPanel();

    if (h - toolbarHeight < UICore::RowHeight())
        return;

    UICore::BeginPanel("Console", x, y + toolbarHeight, w, h - toolbarHeight);
        size_t count = Log::GetHistoryCount();

        for (size_t i = 0; i < count; i++) {
            int severity = Log::GetHistorySeverity(i);

            Uint32 color = UI_COL_TEXT_DIM;
            switch (severity) {
                case Log::LOG_VERBOSE:
                    if (!ShowVerbose)
                        continue;
                    color = UI_COL_VERBOSE;
                    break;
                case Log::LOG_WARN:
                    if (!ShowWarnings)
                        continue;
                    color = UI_COL_WARNING;
                    break;
                case Log::LOG_ERROR:
                    if (!ShowErrors)
                        continue;
                    color = UI_COL_DANGER;
                    break;
                case Log::LOG_IMPORTANT:
                    color = UI_COL_ACCENT;
                    break;
                default:
                    if (!ShowInfo)
                        continue;
                    break;
            }

            Studio::DrawLogLine(Log::GetHistoryText(i), color);
        }

        // Snap to the newest message whenever something was logged, so the
        // console behaves like a terminal that is being tailed.
        if (FollowLog && count != LastLogCount) {
            UICore::ScrollToBottom();
            LastLogCount = count;
        }
    UICore::EndPanel();
}

// Log messages can be several lines long and wider than the panel, so break
// them up rather than letting them run off the edge.
PRIVATE STATIC void Studio::DrawLogLine(const char* text, Uint32 color) {
    int columns = (int)(UICore::ContentWidth() / UIDraw::CharWidth());
    if (columns < 8)
        columns = 8;

    char chunk[1024];
    if (columns > (int)sizeof(chunk) - 1)
        columns = (int)sizeof(chunk) - 1;

    const char* cursor = text;
    while (*cursor) {
        int length = 0;
        while (cursor[length] && cursor[length] != '\n' && length < columns)
            length++;

        memcpy(chunk, cursor, length);
        chunk[length] = '\0';
        UICore::Text(chunk, color);

        cursor += length;
        if (*cursor == '\n')
            cursor++;
    }
}

PRIVATE STATIC void Studio::DrawHelpTab(float x, float y, float w, float h) {
    UICore::BeginPanel("About & Controls", x, y, w, h);
        UICore::Heading("Hatch Game Engine");
        UICore::Field("Engine version", Application::EngineVersion);
        UICore::Field("Renderer", Graphics::Renderer ? Graphics::Renderer : "unknown");
        UICore::Field("Settings file", Application::SettingsFile);

        UICore::Separator();
        UICore::Heading("Using the editor");
        UICore::Text("F12 or the ` key opens and closes this editor.", UI_COL_TEXT_DIM);
        UICore::Text("Project    pick a game folder or .hatch file, or make a new one.", UI_COL_TEXT_DIM);
        UICore::Text("Scenes     browse the project's scenes and load one.", UI_COL_TEXT_DIM);
        UICore::Text("Play       pause, step, reload, and inspect the running scene.", UI_COL_TEXT_DIM);
        UICore::Text("Settings   change display, audio and developer options.", UI_COL_TEXT_DIM);
        UICore::Text("Console    read the engine log without a terminal.", UI_COL_TEXT_DIM);

        UICore::Separator();
        UICore::Heading("Developer hotkeys");

        if (!Application::IsDevMenuEnabled())
            UICore::Text("Currently off; turn them on under Settings.", UI_COL_WARNING);

        struct {
            const char* Name;
            KeyBind     Bind;
        } binds[] = {
            { "Toggle fullscreen",          KeyBind::Fullscreen },
            { "Restart engine",             KeyBind::DevRestartApp },
            { "Restart scene",              KeyBind::DevRestartScene },
            { "Recompile and reload scene", KeyBind::DevRecompile },
            { "Performance snapshot",       KeyBind::DevPerfSnapshot },
            { "Log layer info",             KeyBind::DevLayerInfo },
            { "Fast forward",               KeyBind::DevFastForward },
            { "Toggle frame stepper",       KeyBind::DevFrameStepper },
            { "Step one frame",             KeyBind::DevStepFrame },
            { "Cycle tile collision view",  KeyBind::DevTileCol },
            { "Show object regions",        KeyBind::DevObjectRegions },
            { "Quit",                       KeyBind::DevQuit },
        };

        for (size_t i = 0; i < sizeof(binds) / sizeof(binds[0]); i++) {
            char* keyName = InputManager::GetKeyName(Application::GetKeyBind((int)binds[i].Bind));
            UICore::Field(binds[i].Name, keyName ? keyName : "unbound");
        }
    UICore::EndPanel();
}

// ---------------------------------------------------------------- rendering -

PUBLIC STATIC void Studio::Render() {
    if (!Studio::Visible || !Initialized)
        return;

    // The selection holds a raw pointer into a list the scene owns and frees.
    // Checking it here, once, means no panel below has to remember to.
    Selection::Validate();

    UICore::NewFrame();

    // Begin declines the frame when the window has no size yet, or before the
    // scene has set up the view the renderer projects through.
    if (!UIDraw::IsInFrame()) {
        UICore::EndFrame();
        return;
    }

    float scale = UIDraw::Scale;
    float width = (float)UIDraw::ViewWidth;
    float height = (float)UIDraw::ViewHeight;

    bool editing = CurrentTab == STUDIO_TAB_EDITOR && SceneEditor::HasScene();

    // Everywhere but the editor, the game is dimmed so the panels read clearly.
    // The editor needs to see what it is editing, so it keeps the view.
    if (!editing)
        UICore::Backdrop();

    Studio::HandleShortcuts();

    float menuHeight = UICore::TitleBarHeight();
    float tabHeight = UICore::RowHeight();
    float statusHeight = UIDraw::LineHeight() + 6.0f * scale;

    // An open menu, dropdown list or dialog takes the mouse for itself, so that
    // a click on it cannot also reach whatever it is covering.
    bool modal = UIFileDialog::IsOpen() || ResourceEditor::IsConfirming();
    UICore::SetInputBlocked(modal || UIMenu::IsOpen() || UICore::IsDropdownOpen());

    // Tabs.
    UICore::TabBar(TabNames, STUDIO_TAB_COUNT, &CurrentTab, 0.0f, menuHeight, width, tabHeight);

    // Content.
    float contentY = menuHeight + tabHeight;
    float contentH = height - contentY - statusHeight;

    // Side-by-side panels need enough room to stay readable; below that they
    // stack instead.
    bool split = width >= 96.0f * UIDraw::CharWidth();

    // In a window too short for even one row there is nowhere to put the
    // panels, so only the header, tabs and status bar are drawn.
    if (contentH < UICore::RowHeight())
        contentH = 0.0f;

    switch (contentH > 0.0f ? CurrentTab : -1) {
        case STUDIO_TAB_PROJECT:
            Studio::DrawProjectTab(0.0f, contentY, width, contentH, split);
            break;
        case STUDIO_TAB_HIERARCHY:
            Studio::DrawHierarchyTab(0.0f, contentY, width, contentH, split);
            break;
        case STUDIO_TAB_SCENES:
            Studio::DrawScenesTab(0.0f, contentY, width, contentH, split);
            break;
        case STUDIO_TAB_EDITOR:
            SceneEditor::Draw(0.0f, contentY, width, contentH);
            break;
        case STUDIO_TAB_COLLISION:
            TileCollisionEditor::Draw(0.0f, contentY, width, contentH, split);
            break;
        case STUDIO_TAB_3D:
            SceneEditor3D::Draw(0.0f, contentY, width, contentH, split);
            break;
        case STUDIO_TAB_RESOURCES:
            ResourceEditor::Draw(0.0f, contentY, width, contentH, split);
            break;
        case STUDIO_TAB_PLAY:
            Studio::DrawPlayTab(0.0f, contentY, width, contentH, split);
            break;
        case STUDIO_TAB_SETTINGS:
            Studio::DrawSettingsTab(0.0f, contentY, width, contentH, split);
            break;
        case STUDIO_TAB_CONSOLE:
            Studio::DrawConsoleTab(0.0f, contentY, width, contentH);
            break;
        case STUDIO_TAB_HELP:
            Studio::DrawHelpTab(0.0f, contentY, width, contentH);
            break;
    }

    // Status bar.
    float statusY = height - statusHeight;
    UIDraw::FillRect(0.0f, statusY, width, statusHeight, UI_COL_PANEL_HEADER);
    UIDraw::FillRect(0.0f, statusY, width, 1.0f, UI_COL_BORDER);

    bool statusFresh = StatusMessage[0] && (SDL_GetTicks() - StatusMessageTime) < 6000;
    const char* statusLine = statusFresh ? StatusMessage : "F12 or ` closes the editor.";

    if (!statusFresh && CurrentTab == STUDIO_TAB_EDITOR && SceneEditor::GetStatus()[0])
        statusLine = SceneEditor::GetStatus();
    else if (!statusFresh && CurrentTab == STUDIO_TAB_3D && SceneEditor3D::GetStatus()[0])
        statusLine = SceneEditor3D::GetStatus();
    else if (!statusFresh && CurrentTab == STUDIO_TAB_COLLISION && TileCollisionEditor::GetStatus()[0])
        statusLine = TileCollisionEditor::GetStatus();

    UIDraw::Text(UICore::Pad() * 2.0f, statusY + 3.0f * scale, statusLine,
        statusFresh ? UI_COL_SUCCESS : UI_COL_TEXT_FAINT);

    char runState[200];
    snprintf(runState, sizeof(runState), "%s%s%s  |  %.0f FPS",
        SceneEditor::UnsavedChanges ? "scene edited  |  " : "",
        TileCollisionEditor::UnsavedChanges ? "collision edited  |  " : "",
        Studio::IsPausingGame() ? "game paused" : "game running", Application::FPS);
    UIDraw::TextRight(width - UICore::Pad() * 2.0f, statusY + 3.0f * scale,
        runState, UI_COL_TEXT_FAINT, scale);

    // The menu bar and anything else that hangs over the panels is painted from
    // here on, with input let back through so the menus can be used.
    UICore::SetInputBlocked(modal);

    Studio::DrawMenuBar(width, menuHeight);

    // The game's name and the engine version share the menu strip, off to the
    // right where no menu title reaches.
    char versionText[544];
    snprintf(versionText, sizeof(versionText), "%s  |  engine %s",
        Application::GameTitle, Application::EngineVersion);
    UIDraw::TextRight(width - UICore::Pad() * 2.0f, (menuHeight - UIDraw::LineHeight()) / 2.0f,
        versionText, UI_COL_TEXT_FAINT, scale);

    UICore::DrawOverlays();

    UICore::SetInputBlocked(false);

    if (UIFileDialog::Draw() == UIFileDialog::RESULT_ACCEPTED)
        Studio::FinishBrowse(UIFileDialog::GetSelectedPath());

    ResourceEditor::DrawConfirmation();

    // Something the user agreed to lose their scene edits for.
    char approvedArgument[1024];
    switch (ResourceEditor::TakeApprovedAction(approvedArgument, sizeof(approvedArgument))) {
        case ResourceEditor::PENDING_OPEN_SCENE:
            Application::QueueSceneChange(approvedArgument);
            break;
        case ResourceEditor::PENDING_CLOSE_PROJECT:
            Studio::QueueAction(STUDIO_ACTION_CLOSE_PROJECT);
            break;
        case ResourceEditor::PENDING_OPEN_PROJECT:
            Studio::OpenProject(approvedArgument);
            break;
    }

    UICore::EndFrame();
}
