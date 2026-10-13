#ifndef ENGINE_UI_UIMENU_H
#define ENGINE_UI_UIMENU_H

#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>

class UIMenu {
private:
    static float RowHeight();
    static float MeasureItem(const char* label, const char* shortcut);
    static void TrackWidth(float width);
    static void PlaceRow(float height, float* outX, float* outY, float* outWidth);
    static bool DrawRow(const char* label, const char* shortcut, const char* mark, bool enabled, bool submenuArrow);

public:
    static bool IsOpen();
    static void Close();
    static void Begin(float x, float y, float width, float height);
    static void End();
    static bool BeginMenu(const char* label);
    static void EndMenu();
    static bool Item(const char* label, const char* shortcut, bool enabled);
    static bool CheckItem(const char* label, const char* shortcut, bool checked, bool enabled);
    static bool RadioItem(const char* label, bool selected, bool enabled);
    static void Separator();
    static bool BeginSubmenu(const char* label);
    static void EndSubmenu();
};

#endif /* ENGINE_UI_UIMENU_H */
