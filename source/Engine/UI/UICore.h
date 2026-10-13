#ifndef ENGINE_UI_UICORE_H
#define ENGINE_UI_UICORE_H

#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>

class UICore {
private:
    static Uint32 Hash(const char* text, Uint32 seed);
    static Uint32 ItemID(const char* label);
    static const char* VisibleLabel(const char* label, char* buffer, size_t bufferSize);
    static void FlushLine();
    static void PlaceItem(float width, float height, float* outX, float* outY);
    static float TakeItemWidth();
    static char* NumberBuffer(Uint32 id, const char* formatted, char* scratch, size_t scratchSize);

public:
    static float MouseX;
    static float MouseY;
    static bool  MouseIsDown;
    static bool  MouseWasPressed;
    static bool  MouseWasReleased;
    static float MouseWheel;
    // True while a text field has focus, so the caller knows to keep key
    // presses away from the game.
    static bool  WantsKeyboard;

    static float Pad();
    static float RowHeight();
    static float TitleBarHeight();
    static Uint32 MakeID(const char* label);
    static Uint32 MakeID(const char* label, Uint32 scope);
    static bool HandleEvent(SDL_Event* event);
    static void ClearFocus();
    static void Reset();
    static void NewFrame();
    static void EndFrame();
    static bool IsOver(float x, float y, float w, float h);
    static bool ClickableRegion(Uint32 id, float x, float y, float w, float h, bool* outHovered, bool* outHeld);
    static void SetInputBlocked(bool blocked);
    static bool IsItemHovered();
    static bool WasShortcutPressed(SDL_Keycode key, Uint16 modifiers);
    static void PlaceCustomItem(float width, float height, float* outX, float* outY);
    static void SetNextItemWidth(float width);
    static void SameLine();
    static float ContentWidth();
    static void Separator();
    static void BeginPanel(const char* title, float x, float y, float w, float h);
    static void EndPanel();
    static void ScrollToBottom();
    static void Text(const char* text, Uint32 color);
    static void Text(const char* text);
    static void Heading(const char* text);
    static void Field(const char* name, const char* value);
    static void FieldFormatted(const char* name, const char* format, ...);
    static bool Button(const char* label, float width);
    static bool Button(const char* label);
    static bool ButtonEnabled(const char* label, bool enabled);
    static bool Checkbox(const char* label, bool* value);
    static bool SliderInt(const char* label, int* value, int minimum, int maximum);
    static bool ListItem(const char* label, bool selected);
    static void ResetRowStriping();
    static bool FloatField(const char* label, float* value);
    static bool IntField(const char* label, int* value);
    static bool TextField(const char* label, char* buffer, size_t bufferSize);
    static bool TabBar(const char** names, int count, int* current, float x, float y, float width, float height);
    static bool Dropdown(const char* label, const char* const* options, int count, int* current);
    static void Tooltip(const char* text);
    static bool IsDropdownOpen();
    static void CloseDropdown();
    static void DrawOverlays();
    static void Backdrop();
};

#endif /* ENGINE_UI_UICORE_H */
