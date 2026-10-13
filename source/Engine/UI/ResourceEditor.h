#ifndef ENGINE_UI_RESOURCEEDITOR_H
#define ENGINE_UI_RESOURCEEDITOR_H

#include <Engine/Includes/Standard.h>

class ResourceEditor {
private:
    static void SetStatus(const char* format, ...);
    static const char* DescribeFile(const char* name);
    static void ScanFolder(const char* path, const char* relative, int depth);

public:
    // What a pending confirmation is guarding, so the answer can be acted on.
    enum PendingAction {
        PENDING_NONE, 
        PENDING_OPEN_SCENE, 
        PENDING_CLOSE_PROJECT, 
        PENDING_OPEN_PROJECT 
    }; 

    static void Rescan();
    static void Invalidate();
    static bool HasUnsavedChanges();
    static bool RequestAction(int action, const char* argument);
    static bool IsConfirming();
    static int TakeApprovedAction(char* outArgument, size_t outSize);
    static void DrawConfirmation();
    static void Draw(float x, float y, float width, float height, bool split);
};

#endif /* ENGINE_UI_RESOURCEEDITOR_H */
