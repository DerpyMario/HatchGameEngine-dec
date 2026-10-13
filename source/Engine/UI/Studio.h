#ifndef ENGINE_UI_STUDIO_H
#define ENGINE_UI_STUDIO_H

#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>

class Studio {
private:
    static void SetStatus(const char* format, ...);
    static void ScanForProjects();
    static void LoadRecentProjects();
    static void SaveRecentProjects();
    static void ClearRecentProjects();
    static void RememberProject(const char* path);
    static void ScanForSceneFiles();
    static bool CreateScene(const char* name);
    static bool CreateProject(const char* name);
    static void QueueAction(int action);
    static void OpenProject(const char* path);
    static void BrowseFor(int what);
    static void FinishBrowse(const char* path);
    static void Run();
    static void SetRunFromStartScene(bool fromStart);
    static void DrawMenuBar(float width, float height);
    static void NewProjectCommand();
    static void NewSceneCommand();
    static void SaveSettingsCommand();
    static void HandleShortcuts();
    static void DrawProjectTab(float x, float y, float w, float h, bool split);
    static void DrawHierarchyTab(float x, float y, float w, float h, bool split);
    static void DrawScenesTab(float x, float y, float w, float h, bool split);
    static void DrawPlayTab(float x, float y, float w, float h, bool split);
    static void DrawSettingsTab(float x, float y, float w, float h, bool split);
    static void DrawConsoleTab(float x, float y, float w, float h);
    static void DrawLogLine(const char* text, Uint32 color);
    static void DrawHelpTab(float x, float y, float w, float h);

public:
    static bool Visible;
    static bool PauseGameWhileOpen;

    static void Init();
    static void Dispose();
    static bool IsAvailable();
    static void Show();
    static void Hide();
    static void Toggle();
    static bool IsPausingGame();
    static bool HandleEvent(SDL_Event* event);
    static void Update();
    static void Render();
};

#endif /* ENGINE_UI_STUDIO_H */
