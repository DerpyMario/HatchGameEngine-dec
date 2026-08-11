#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>

class UICore {
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
};
#endif

#include <Engine/UI/UICore.h>
#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UITheme.h>

#include <Engine/Diagnostics/Log.h>

// An immediate-mode toolkit: widgets are declared and drawn in the same call,
// and the only state kept between frames is which item the mouse is over, which
// item is being dragged, which text field has focus, and how far each panel is
// scrolled. Everything else is rebuilt every frame from the caller's data.

float UICore::MouseX = 0.0f;
float UICore::MouseY = 0.0f;
bool  UICore::MouseIsDown = false;
bool  UICore::MouseWasPressed = false;
bool  UICore::MouseWasReleased = false;
float UICore::MouseWheel = 0.0f;
bool  UICore::WantsKeyboard = false;

struct UIKeyPress {
    SDL_Keycode Key;
    Uint16      Mod;
};

struct UIPanelState {
    float Scroll = 0.0f;
    float ContentHeight = 0.0f;
};

static Uint32 HotItem = 0;
static Uint32 ActiveItem = 0;
static Uint32 FocusItem = 0;
static Uint32 HoveredThisFrame = 0;

static vector<UIKeyPress> KeyPresses;
static std::string        TypedText;

static std::map<Uint32, UIPanelState> PanelStates;

// Layout state for the panel currently being built.
static bool   InPanel = false;
static Uint32 PanelID = 0;
static float  PanelX = 0.0f;
static float  PanelW = 0.0f;
static float  ContentX = 0.0f;
static float  ContentW = 0.0f;
static float  ContentTop = 0.0f;
static float  ContentBottom = 0.0f;
static float  CursorX = 0.0f;
static float  CursorY = 0.0f;
static float  LineHeightUsed = 0.0f;
static float  NextItemWidth = 0.0f;
static bool   SameLineFlag = false;
static bool   PanelHovered = false;
static int    RowIndex = 0;

// Set while a menu or a dropdown list is on screen. Whatever is underneath the
// popup has to stop reacting to the mouse, or a click meant for a menu item
// would also land on the panel behind it.
static bool   InputBlocked = false;
static bool   LastItemHovered = false;

// Dropdown lists and tooltips have to be painted after the panels they overlap,
// so the widget only records what it wants and DrawOverlays paints it once the
// rest of the frame is done.
static Uint32       OpenDropdownID = 0;
static float        DropdownX = 0.0f;
static float        DropdownY = 0.0f;
static float        DropdownW = 0.0f;
static const char* const* DropdownOptions = NULL;
static int          DropdownCount = 0;
static Uint32       DropdownResultID = 0;
static int          DropdownResultIndex = 0;
static bool         DropdownJustOpened = false;

static const char*  PendingTooltip = NULL;

// Text field editing state, only meaningful for the focused field.
static int    EditCaret = 0;
static Uint32 CaretBlinkStart = 0;

PUBLIC STATIC float UICore::Pad() {
    return 4.0f * UIDraw::Scale;
}

PUBLIC STATIC float UICore::RowHeight() {
    return UIDraw::LineHeight() + 6.0f * UIDraw::Scale;
}

PUBLIC STATIC float UICore::TitleBarHeight() {
    return UIDraw::LineHeight() + 8.0f * UIDraw::Scale;
}

PRIVATE STATIC Uint32 UICore::Hash(const char* text, Uint32 seed) {
    Uint32 hash = seed ? seed : 2166136261U;
    for (const char* i = text; i && *i; i++) {
        hash ^= (Uint32)(unsigned char)*i;
        hash *= 16777619U;
    }
    // Zero doubles as "nothing", so never hand it back as a real ID.
    return hash ? hash : 1U;
}

PRIVATE STATIC Uint32 UICore::ItemID(const char* label) {
    return UICore::Hash(label, PanelID);
}

// An ID for widgets that live outside any panel, such as the menu bar.
PUBLIC STATIC Uint32 UICore::MakeID(const char* label) {
    return UICore::Hash(label, 0);
}

// The same, but scoped to an enclosing widget so that rows sharing a label in
// two different menus do not collide.
PUBLIC STATIC Uint32 UICore::MakeID(const char* label, Uint32 scope) {
    return UICore::Hash(label, scope);
}

// Labels may carry a "##suffix" that makes the widget's ID unique without
// showing up on screen, the same convention Dear ImGui uses.
PRIVATE STATIC const char* UICore::VisibleLabel(const char* label, char* buffer, size_t bufferSize) {
    if (!label)
        return "";

    const char* marker = strstr(label, "##");
    if (!marker)
        return label;

    size_t length = (size_t)(marker - label);
    if (length > bufferSize - 1)
        length = bufferSize - 1;

    memcpy(buffer, label, length);
    buffer[length] = '\0';
    return buffer;
}

// ---------------------------------------------------------------- input ----

PUBLIC STATIC bool UICore::HandleEvent(SDL_Event* event) {
    switch (event->type) {
        case SDL_MOUSEMOTION:
            UICore::MouseX = (float)event->motion.x;
            UICore::MouseY = (float)event->motion.y;
            return true;

        case SDL_MOUSEBUTTONDOWN:
            if (event->button.button != SDL_BUTTON_LEFT)
                return false;
            UICore::MouseX = (float)event->button.x;
            UICore::MouseY = (float)event->button.y;
            UICore::MouseIsDown = true;
            UICore::MouseWasPressed = true;
            return true;

        case SDL_MOUSEBUTTONUP:
            if (event->button.button != SDL_BUTTON_LEFT)
                return false;
            UICore::MouseX = (float)event->button.x;
            UICore::MouseY = (float)event->button.y;
            UICore::MouseIsDown = false;
            UICore::MouseWasReleased = true;
            return true;

        case SDL_MOUSEWHEEL:
            UICore::MouseWheel += (float)event->wheel.y;
            return true;

        case SDL_TEXTINPUT:
            if (!FocusItem)
                return false;
            TypedText += event->text.text;
            return true;

        case SDL_KEYDOWN: {
            UIKeyPress press;
            press.Key = event->key.keysym.sym;
            press.Mod = event->key.keysym.mod;
            KeyPresses.push_back(press);
            // Only swallow the key when a field is being typed into; otherwise
            // the caller still gets to use its own shortcuts.
            return FocusItem != 0;
        }
    }

    return false;
}

PUBLIC STATIC void UICore::ClearFocus() {
    if (FocusItem) {
        FocusItem = 0;
        SDL_StopTextInput();
    }

    UICore::WantsKeyboard = false;
}

PUBLIC STATIC void UICore::Reset() {
    UICore::ClearFocus();

    HotItem = ActiveItem = HoveredThisFrame = 0;
    UICore::MouseIsDown = false;
    UICore::MouseWasPressed = false;
    UICore::MouseWasReleased = false;
    UICore::MouseWheel = 0.0f;

    KeyPresses.clear();
    TypedText.clear();
}

PUBLIC STATIC void UICore::NewFrame() {
    HotItem = HoveredThisFrame;
    HoveredThisFrame = 0;

    UIDraw::Begin();
}

PUBLIC STATIC void UICore::EndFrame() {
    UIDraw::End();

    if (!UICore::MouseIsDown)
        ActiveItem = 0;

    UICore::MouseWasPressed = false;
    UICore::MouseWasReleased = false;
    UICore::MouseWheel = 0.0f;

    KeyPresses.clear();
    TypedText.clear();

    UICore::WantsKeyboard = FocusItem != 0;
}

PUBLIC STATIC bool UICore::IsOver(float x, float y, float w, float h) {
    if (InputBlocked)
        return false;

    if (UICore::MouseX < x || UICore::MouseY < y ||
        UICore::MouseX >= x + w || UICore::MouseY >= y + h)
        return false;

    // A widget scrolled out of its panel is not clickable even though its
    // coordinates still say the mouse is on it.
    if (InPanel && (UICore::MouseY < ContentTop || UICore::MouseY > ContentBottom))
        return false;

    return true;
}

// Shared hit testing for the clickable widgets: marks the item hot, claims it
// on press, and reports a click when the mouse comes back up over it. Public so
// that the menu bar, which draws itself outside the panel layout, behaves the
// same way as everything else.
PUBLIC STATIC bool UICore::ClickableRegion(Uint32 id, float x, float y, float w, float h, bool* outHovered, bool* outHeld) {
    bool hovered = UICore::IsOver(x, y, w, h);
    if (hovered)
        HoveredThisFrame = id;

    LastItemHovered = hovered;

    if (hovered && UICore::MouseWasPressed) {
        ActiveItem = id;
        UICore::ClearFocus();
    }

    bool held = ActiveItem == id;
    bool clicked = held && hovered && UICore::MouseWasReleased;

    if (outHovered)
        *outHovered = hovered;
    if (outHeld)
        *outHeld = held;

    return clicked;
}

// Keeps everything under an open popup from reacting to the mouse.
PUBLIC STATIC void UICore::SetInputBlocked(bool blocked) {
    InputBlocked = blocked;
}

// True when the widget placed just before this call had the mouse over it.
PUBLIC STATIC bool UICore::IsItemHovered() {
    return LastItemHovered;
}

// Reports a key press with an exact modifier combination, for menu shortcuts.
// Only Ctrl, Shift and Alt are considered; Command stands in for Ctrl on macOS.
PUBLIC STATIC bool UICore::WasShortcutPressed(SDL_Keycode key, Uint16 modifiers) {
    for (size_t i = 0; i < KeyPresses.size(); i++) {
        if (KeyPresses[i].Key != key)
            continue;

        Uint16 held = 0;
        if (KeyPresses[i].Mod & (KMOD_CTRL | KMOD_GUI))
            held |= KMOD_CTRL;
        if (KeyPresses[i].Mod & KMOD_SHIFT)
            held |= KMOD_SHIFT;
        if (KeyPresses[i].Mod & KMOD_ALT)
            held |= KMOD_ALT;

        if (held == modifiers)
            return true;
    }

    return false;
}

// --------------------------------------------------------------- layout ----

// Closes off the line being built so the next item starts below it.
PRIVATE STATIC void UICore::FlushLine() {
    if (LineHeightUsed <= 0.0f)
        return;

    CursorX = ContentX;
    CursorY += LineHeightUsed + UICore::Pad();
    LineHeightUsed = 0.0f;
}

// Reserves a rectangle for the next widget and advances the layout cursor.
PRIVATE STATIC void UICore::PlaceItem(float width, float height, float* outX, float* outY) {
    if (!SameLineFlag)
        UICore::FlushLine();
    SameLineFlag = false;

    LastItemHovered = false;

    *outX = CursorX;
    *outY = CursorY;

    CursorX += width + UICore::Pad();
    if (height > LineHeightUsed)
        LineHeightUsed = height;
}

PUBLIC STATIC void UICore::SetNextItemWidth(float width) {
    NextItemWidth = width;
}

// How wide a widget should be when the caller didn't say: the rest of the
// current line.
PRIVATE STATIC float UICore::TakeItemWidth() {
    if (NextItemWidth > 0.0f) {
        float width = NextItemWidth;
        NextItemWidth = 0.0f;
        return width;
    }

    float startX = SameLineFlag ? CursorX : ContentX;
    float width = ContentX + ContentW - startX;
    return width > 0.0f ? width : 0.0f;
}

// Keeps the next widget on the same line as the one just placed.
PUBLIC STATIC void UICore::SameLine() {
    SameLineFlag = true;
}

PUBLIC STATIC float UICore::ContentWidth() {
    return ContentW;
}

PUBLIC STATIC void UICore::Separator() {
    UICore::FlushLine();

    float y = CursorY + UICore::Pad();
    UIDraw::FillRect(ContentX, y, ContentW, 1.0f, UI_COL_BORDER);
    CursorY = y + 1.0f + UICore::Pad();
}

// ---------------------------------------------------------------- panel ----

PUBLIC STATIC void UICore::BeginPanel(const char* title, float x, float y, float w, float h) {
    char labelBuffer[256];
    const char* visible = UICore::VisibleLabel(title, labelBuffer, sizeof(labelBuffer));

    PanelID = UICore::Hash(title, 0);
    PanelX = x;
    PanelW = w;
    InPanel = true;
    RowIndex = 0;
    NextItemWidth = 0.0f;
    LineHeightUsed = 0.0f;
    SameLineFlag = false;

    float titleHeight = *visible ? UICore::TitleBarHeight() : 0.0f;

    UIDraw::FillRect(x, y, w, h, UI_COL_PANEL);
    UIDraw::StrokeRect(x, y, w, h, UI_COL_BORDER);

    if (titleHeight > 0.0f) {
        UIDraw::FillRect(x + 1.0f, y + 1.0f, w - 2.0f, titleHeight - 1.0f, UI_COL_PANEL_HEADER);
        UIDraw::FillRect(x + 1.0f, y + titleHeight, w - 2.0f, 1.0f, UI_COL_BORDER);
        UIDraw::Text(x + UICore::Pad() * 2.0f, y + 4.0f * UIDraw::Scale, visible, UI_COL_TEXT);
    }

    UIPanelState& state = PanelStates[PanelID];

    ContentTop = y + titleHeight + 1.0f;
    ContentBottom = y + h - 1.0f;
    ContentX = x + UICore::Pad() * 2.0f;
    ContentW = w - UICore::Pad() * 4.0f;

    PanelHovered = UICore::MouseX >= x && UICore::MouseX < x + w &&
        UICore::MouseY >= ContentTop && UICore::MouseY < ContentBottom;

    float viewHeight = ContentBottom - ContentTop;

    // Reserve room for the scrollbar only once the content actually overflows.
    bool scrollable = state.ContentHeight > viewHeight;
    if (scrollable)
        ContentW -= 6.0f * UIDraw::Scale;

    if (scrollable && PanelHovered && UICore::MouseWheel != 0.0f) {
        state.Scroll -= UICore::MouseWheel * UICore::RowHeight() * 2.0f;
        UICore::MouseWheel = 0.0f;
    }

    float maxScroll = state.ContentHeight - viewHeight;
    if (maxScroll < 0.0f)
        maxScroll = 0.0f;
    if (state.Scroll > maxScroll)
        state.Scroll = maxScroll;
    if (state.Scroll < 0.0f)
        state.Scroll = 0.0f;

    CursorX = ContentX;
    CursorY = ContentTop + UICore::Pad() - state.Scroll;

    UIDraw::PushClip(x + 1.0f, ContentTop, w - 2.0f, viewHeight);
}

PUBLIC STATIC void UICore::EndPanel() {
    UICore::FlushLine();

    UIPanelState& state = PanelStates[PanelID];
    state.ContentHeight = (CursorY + state.Scroll) - ContentTop + UICore::Pad();

    UIDraw::PopClip();

    float viewHeight = ContentBottom - ContentTop;
    if (state.ContentHeight > viewHeight && viewHeight > 0.0f) {
        float barX = PanelX + PanelW - 5.0f * UIDraw::Scale - 1.0f;
        float barW = 4.0f * UIDraw::Scale;

        UIDraw::FillRect(barX, ContentTop, barW, viewHeight, UI_COL_FIELD);

        float thumbHeight = viewHeight * (viewHeight / state.ContentHeight);
        if (thumbHeight < 12.0f * UIDraw::Scale)
            thumbHeight = 12.0f * UIDraw::Scale;

        float maxScroll = state.ContentHeight - viewHeight;
        float thumbY = ContentTop + (viewHeight - thumbHeight) * (state.Scroll / maxScroll);

        Uint32 id = UICore::Hash("##scrollbar", PanelID);
        bool hovered = false, held = false;
        UICore::ClickableRegion(id, barX, ContentTop, barW, viewHeight, &hovered, &held);

        if (held) {
            float travel = viewHeight - thumbHeight;
            if (travel > 0.0f) {
                float target = (UICore::MouseY - ContentTop - thumbHeight / 2.0f) / travel;
                state.Scroll = target * maxScroll;
                if (state.Scroll < 0.0f)
                    state.Scroll = 0.0f;
                if (state.Scroll > maxScroll)
                    state.Scroll = maxScroll;
            }
        }

        UIDraw::FillRect(barX, thumbY, barW, thumbHeight,
            (held || hovered) ? UI_COL_ACCENT : UI_COL_BORDER_LIGHT);
    }
    else
        state.Scroll = 0.0f;

    InPanel = false;
    PanelHovered = false;
}

// Pins the panel being built to the end of its content, so the console keeps
// following new log lines.
PUBLIC STATIC void UICore::ScrollToBottom() {
    PanelStates[PanelID].Scroll = PanelStates[PanelID].ContentHeight;
}

// --------------------------------------------------------------- widgets ---

PUBLIC STATIC void UICore::Text(const char* text, Uint32 color) {
    float width = UICore::TakeItemWidth();
    float x, y;
    UICore::PlaceItem(width, UIDraw::LineHeight(), &x, &y);

    UIDraw::TextClipped(x, y, text, color, UIDraw::Scale, width);
}

PUBLIC STATIC void UICore::Text(const char* text) {
    UICore::Text(text, UI_COL_TEXT);
}

PUBLIC STATIC void UICore::Heading(const char* text) {
    float width = UICore::TakeItemWidth();
    float x, y;
    UICore::PlaceItem(width, UIDraw::LineHeight() + 3.0f * UIDraw::Scale, &x, &y);

    UIDraw::Text(x, y, text, UI_COL_ACCENT);
    UIDraw::FillRect(x, y + UIDraw::LineHeight() + 2.0f * UIDraw::Scale, width, 1.0f, UI_COL_BORDER);
}

// Draws a "name  value" row with the value right-aligned, used all over the
// inspector panels.
PUBLIC STATIC void UICore::Field(const char* name, const char* value) {
    float width = UICore::TakeItemWidth();
    float x, y;
    UICore::PlaceItem(width, UIDraw::LineHeight(), &x, &y);

    UIDraw::Text(x, y, name, UI_COL_TEXT_DIM);

    float nameEnd = x + UIDraw::TextWidth(name) + UICore::Pad() * 2.0f;
    float available = x + width - nameEnd;
    float valueWidth = UIDraw::TextWidth(value);

    if (valueWidth > available)
        UIDraw::TextClipped(nameEnd, y, value, UI_COL_TEXT, UIDraw::Scale, available);
    else
        UIDraw::Text(x + width - valueWidth, y, value, UI_COL_TEXT);
}

PUBLIC STATIC void UICore::FieldFormatted(const char* name, const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    UICore::Field(name, buffer);
}

PUBLIC STATIC bool UICore::Button(const char* label, float width) {
    char labelBuffer[256];
    const char* visible = UICore::VisibleLabel(label, labelBuffer, sizeof(labelBuffer));

    if (width <= 0.0f)
        width = UIDraw::TextWidth(visible) + UICore::Pad() * 4.0f;

    float x, y;
    float height = UICore::RowHeight();
    UICore::PlaceItem(width, height, &x, &y);

    Uint32 id = UICore::ItemID(label);
    bool hovered = false, held = false;
    bool clicked = UICore::ClickableRegion(id, x, y, width, height, &hovered, &held);

    Uint32 fill = UI_COL_BUTTON;
    if (held && hovered)
        fill = UI_COL_BUTTON_DOWN;
    else if (hovered)
        fill = UI_COL_BUTTON_HOVER;

    UIDraw::BeveledRect(x, y, width, height, fill, UI_COL_BORDER_LIGHT, UI_COL_BORDER);
    UIDraw::TextCentered(x + width / 2.0f, y + (height - UIDraw::LineHeight()) / 2.0f,
        visible, (held && hovered) ? UI_COL_TEXT_ON_ACC : UI_COL_TEXT, UIDraw::Scale);

    return clicked;
}

PUBLIC STATIC bool UICore::Button(const char* label) {
    return UICore::Button(label, UICore::TakeItemWidth());
}

// A button that is dimmed and unclickable, for actions that don't apply yet.
PUBLIC STATIC bool UICore::ButtonEnabled(const char* label, bool enabled) {
    float width = UICore::TakeItemWidth();

    if (enabled)
        return UICore::Button(label, width);

    char labelBuffer[256];
    const char* visible = UICore::VisibleLabel(label, labelBuffer, sizeof(labelBuffer));

    float x, y;
    float height = UICore::RowHeight();
    UICore::PlaceItem(width, height, &x, &y);

    UIDraw::BeveledRect(x, y, width, height, UI_COL_BUTTON_OFF, UI_COL_BORDER, UI_COL_BORDER);
    UIDraw::TextCentered(x + width / 2.0f, y + (height - UIDraw::LineHeight()) / 2.0f,
        visible, UI_COL_TEXT_FAINT, UIDraw::Scale);

    return false;
}

PUBLIC STATIC bool UICore::Checkbox(const char* label, bool* value) {
    char labelBuffer[256];
    const char* visible = UICore::VisibleLabel(label, labelBuffer, sizeof(labelBuffer));

    float width = UICore::TakeItemWidth();
    float x, y;
    float height = UICore::RowHeight();
    UICore::PlaceItem(width, height, &x, &y);

    Uint32 id = UICore::ItemID(label);
    bool hovered = false, held = false;
    bool clicked = UICore::ClickableRegion(id, x, y, width, height, &hovered, &held);

    if (clicked)
        *value = !*value;

    float box = UIDraw::LineHeight();
    float boxY = y + (height - box) / 2.0f;

    UIDraw::FillRect(x, boxY, box, box, *value ? UI_COL_ACCENT : UI_COL_FIELD);
    UIDraw::StrokeRect(x, boxY, box, box, hovered ? UI_COL_ACCENT : UI_COL_BORDER_LIGHT);

    if (*value) {
        float inset = 2.0f * UIDraw::Scale;
        UIDraw::FillRect(x + inset, boxY + inset, box - inset * 2.0f, box - inset * 2.0f, UI_COL_TEXT_ON_ACC);
    }

    UIDraw::TextClipped(x + box + UICore::Pad() * 2.0f, y + (height - UIDraw::LineHeight()) / 2.0f,
        visible, hovered ? UI_COL_TEXT : UI_COL_TEXT_DIM, UIDraw::Scale,
        width - box - UICore::Pad() * 2.0f);

    return clicked;
}

PUBLIC STATIC bool UICore::SliderInt(const char* label, int* value, int minimum, int maximum) {
    char labelBuffer[256];
    const char* visible = UICore::VisibleLabel(label, labelBuffer, sizeof(labelBuffer));

    float width = UICore::TakeItemWidth();
    float x, y;
    float height = UICore::RowHeight();
    UICore::PlaceItem(width, height, &x, &y);

    Uint32 id = UICore::ItemID(label);

    float labelWidth = width * 0.42f;
    float trackX = x + labelWidth;
    float trackW = width - labelWidth;

    bool hovered = false, held = false;
    UICore::ClickableRegion(id, trackX, y, trackW, height, &hovered, &held);

    int before = *value;
    if (held && trackW > 0.0f) {
        float fraction = (UICore::MouseX - trackX) / trackW;
        if (fraction < 0.0f)
            fraction = 0.0f;
        if (fraction > 1.0f)
            fraction = 1.0f;

        *value = minimum + (int)(fraction * (maximum - minimum) + 0.5f);
    }

    if (*value < minimum)
        *value = minimum;
    if (*value > maximum)
        *value = maximum;

    float span = (float)(maximum - minimum);
    float filled = span > 0.0f ? (*value - minimum) / span : 0.0f;
    float barY = y + (height - UIDraw::LineHeight()) / 2.0f;
    float barH = UIDraw::LineHeight();

    UIDraw::TextClipped(x, barY, visible, UI_COL_TEXT_DIM, UIDraw::Scale, labelWidth - UICore::Pad());
    UIDraw::FillRect(trackX, barY, trackW, barH, UI_COL_FIELD);
    UIDraw::FillRect(trackX, barY, trackW * filled, barH, held ? UI_COL_ACCENT : UI_COL_ACCENT_DIM);
    UIDraw::StrokeRect(trackX, barY, trackW, barH, hovered ? UI_COL_ACCENT : UI_COL_BORDER_LIGHT);

    char readout[32];
    snprintf(readout, sizeof(readout), "%d", *value);
    UIDraw::TextCentered(trackX + trackW / 2.0f, barY, readout, UI_COL_TEXT, UIDraw::Scale);

    return *value != before;
}

// A full-width row that reports a click, used for scene and project lists.
PUBLIC STATIC bool UICore::ListItem(const char* label, bool selected) {
    char labelBuffer[256];
    const char* visible = UICore::VisibleLabel(label, labelBuffer, sizeof(labelBuffer));

    float width = UICore::TakeItemWidth();
    float x, y;
    float height = UIDraw::LineHeight() + 4.0f * UIDraw::Scale;
    UICore::PlaceItem(width, height, &x, &y);

    Uint32 id = UICore::ItemID(label);
    bool hovered = false, held = false;
    bool clicked = UICore::ClickableRegion(id, x, y, width, height, &hovered, &held);

    if (selected)
        UIDraw::FillRect(x, y, width, height, UI_COL_SELECTION);
    else if (hovered)
        UIDraw::FillRect(x, y, width, height, UI_COL_BUTTON);
    else if (RowIndex & 1)
        UIDraw::FillRect(x, y, width, height, UI_COL_ROW_ALT);

    UIDraw::TextClipped(x + UICore::Pad(), y + 2.0f * UIDraw::Scale, visible,
        (selected || hovered) ? UI_COL_TEXT : UI_COL_TEXT_DIM, UIDraw::Scale,
        width - UICore::Pad() * 2.0f);

    RowIndex++;

    return clicked;
}

PUBLIC STATIC void UICore::ResetRowStriping() {
    RowIndex = 0;
}

PUBLIC STATIC bool UICore::TextField(const char* label, char* buffer, size_t bufferSize) {
    char labelBuffer[256];
    const char* visible = UICore::VisibleLabel(label, labelBuffer, sizeof(labelBuffer));

    float width = UICore::TakeItemWidth();
    float x, y;
    float height = UICore::RowHeight();
    UICore::PlaceItem(width, height, &x, &y);

    Uint32 id = UICore::ItemID(label);

    float labelWidth = *visible ? width * 0.32f : 0.0f;
    float fieldX = x + labelWidth;
    float fieldW = width - labelWidth;

    bool hovered = UICore::IsOver(fieldX, y, fieldW, height);
    if (hovered)
        HoveredThisFrame = id;

    LastItemHovered = hovered;

    if (UICore::MouseWasPressed) {
        if (hovered) {
            FocusItem = id;
            EditCaret = (int)strlen(buffer);
            CaretBlinkStart = SDL_GetTicks();
            SDL_StartTextInput();
        }
        else if (FocusItem == id)
            UICore::ClearFocus();
    }

    bool focused = FocusItem == id;
    bool submitted = false;

    if (focused) {
        int length = (int)strlen(buffer);
        if (EditCaret > length)
            EditCaret = length;

        for (size_t i = 0; i < KeyPresses.size(); i++) {
            SDL_Keycode key = KeyPresses[i].Key;
            Uint16 mod = KeyPresses[i].Mod;
            length = (int)strlen(buffer);

            // Clipboard. There is no selection to speak of, so copy and cut
            // take the whole field and paste drops in at the caret.
            if (mod & (KMOD_CTRL | KMOD_GUI)) {
                if (key == SDLK_c || key == SDLK_x) {
                    SDL_SetClipboardText(buffer);

                    if (key == SDLK_x) {
                        buffer[0] = '\0';
                        EditCaret = 0;
                    }
                    continue;
                }

                if (key == SDLK_v && SDL_HasClipboardText()) {
                    char* pasted = SDL_GetClipboardText();
                    if (pasted) {
                        for (char* c = pasted; *c; c++) {
                            length = (int)strlen(buffer);
                            if (*c < 0x20 || length + 1 >= (int)bufferSize)
                                continue;

                            memmove(buffer + EditCaret + 1, buffer + EditCaret,
                                length - EditCaret + 1);
                            buffer[EditCaret] = *c;
                            EditCaret++;
                        }
                        SDL_free(pasted);
                    }
                    continue;
                }
            }

            if (key == SDLK_BACKSPACE && EditCaret > 0) {
                memmove(buffer + EditCaret - 1, buffer + EditCaret, length - EditCaret + 1);
                EditCaret--;
            }
            else if (key == SDLK_DELETE && EditCaret < length)
                memmove(buffer + EditCaret, buffer + EditCaret + 1, length - EditCaret);
            else if (key == SDLK_LEFT && EditCaret > 0)
                EditCaret--;
            else if (key == SDLK_RIGHT && EditCaret < length)
                EditCaret++;
            else if (key == SDLK_HOME)
                EditCaret = 0;
            else if (key == SDLK_END)
                EditCaret = length;
            else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                submitted = true;
                UICore::ClearFocus();
                focused = false;
                break;
            }
            else if (key == SDLK_ESCAPE) {
                UICore::ClearFocus();
                focused = false;
                break;
            }
        }

        if (focused && TypedText.size()) {
            length = (int)strlen(buffer);
            for (size_t i = 0; i < TypedText.size(); i++) {
                char typed = TypedText[i];
                if (typed < 0x20 || length + 1 >= (int)bufferSize)
                    continue;

                memmove(buffer + EditCaret + 1, buffer + EditCaret, length - EditCaret + 1);
                buffer[EditCaret] = typed;
                EditCaret++;
                length++;
            }
            TypedText.clear();
        }
    }

    if (*visible)
        UIDraw::TextClipped(x, y + (height - UIDraw::LineHeight()) / 2.0f, visible,
            UI_COL_TEXT_DIM, UIDraw::Scale, labelWidth - UICore::Pad());

    UIDraw::FillRect(fieldX, y, fieldW, height, UI_COL_FIELD);
    UIDraw::StrokeRect(fieldX, y, fieldW, height, focused ? UI_COL_ACCENT :
        (hovered ? UI_COL_BORDER_LIGHT : UI_COL_BORDER));

    float textY = y + (height - UIDraw::LineHeight()) / 2.0f;
    float textX = fieldX + UICore::Pad();
    float textArea = fieldW - UICore::Pad() * 2.0f;

    // Scroll the visible window of text so the caret stays inside the field.
    int visibleChars = (int)(textArea / UIDraw::CharWidth());
    if (visibleChars < 1)
        visibleChars = 1;

    int firstChar = 0;
    if (focused && EditCaret > visibleChars)
        firstChar = EditCaret - visibleChars;

    UIDraw::TextClipped(textX, textY, buffer + firstChar, UI_COL_TEXT, UIDraw::Scale, textArea);

    if (focused && (SDL_GetTicks() - CaretBlinkStart) % 1000 < 500) {
        float caretX = textX + (EditCaret - firstChar) * UIDraw::CharWidth();
        UIDraw::FillRect(caretX, textY, 1.0f * UIDraw::Scale, UIDraw::LineHeight(), UI_COL_ACCENT);
    }

    return submitted;
}

// A horizontal row of tabs; returns true when the selection changes. Drawn at
// an explicit position because it sits outside any panel.
PUBLIC STATIC bool UICore::TabBar(const char** names, int count, int* current, float x, float y, float width, float height) {
    bool changed = false;
    float tabWidth = count > 0 ? width / count : width;

    UIDraw::FillRect(x, y, width, height, UI_COL_PANEL_HEADER);
    UIDraw::FillRect(x, y + height - 1.0f, width, 1.0f, UI_COL_BORDER);

    Uint32 seed = UICore::Hash("##tabbar", 0);

    for (int i = 0; i < count; i++) {
        float tabX = x + i * tabWidth;
        bool selected = *current == i;

        bool hovered = false, held = false;
        bool clicked = UICore::ClickableRegion(UICore::Hash(names[i], seed), tabX, y, tabWidth, height, &hovered, &held);

        if (clicked && !selected) {
            *current = i;
            changed = true;
        }

        if (selected) {
            UIDraw::FillRect(tabX, y, tabWidth, height, UI_COL_PANEL);
            UIDraw::FillRect(tabX, y, tabWidth, 2.0f * UIDraw::Scale, UI_COL_ACCENT);
        }
        else if (hovered)
            UIDraw::FillRect(tabX, y, tabWidth, height - 1.0f, UI_COL_BUTTON);

        UIDraw::TextCentered(tabX + tabWidth / 2.0f, y + (height - UIDraw::LineHeight()) / 2.0f,
            names[i], selected ? UI_COL_TEXT : UI_COL_TEXT_DIM, UIDraw::Scale);

        if (i > 0)
            UIDraw::FillRect(tabX, y + 2.0f, 1.0f, height - 4.0f, UI_COL_BORDER);
    }

    return changed;
}

// A closed dropdown box showing the current choice. The list itself is painted
// later by DrawOverlays, so it can hang over the panel it sits in.
//
// The options array is read again after this call returns, so it has to outlive
// the frame -- pass a static table, not a local one.
PUBLIC STATIC bool UICore::Dropdown(const char* label, const char* const* options, int count, int* current) {
    char labelBuffer[256];
    const char* visible = UICore::VisibleLabel(label, labelBuffer, sizeof(labelBuffer));

    float width = UICore::TakeItemWidth();
    float x, y;
    float height = UICore::RowHeight();
    UICore::PlaceItem(width, height, &x, &y);

    Uint32 id = UICore::ItemID(label);

    bool changed = false;
    if (DropdownResultID == id) {
        DropdownResultID = 0;
        if (DropdownResultIndex != *current) {
            *current = DropdownResultIndex;
            changed = true;
        }
    }

    float labelWidth = *visible ? width * 0.32f : 0.0f;
    float boxX = x + labelWidth;
    float boxW = width - labelWidth;

    bool hovered = false, held = false;
    if (UICore::ClickableRegion(id, boxX, y, boxW, height, &hovered, &held)) {
        // Clicking the box a second time puts the list away again.
        OpenDropdownID = OpenDropdownID == id ? 0 : id;
        DropdownJustOpened = OpenDropdownID == id;

        if (OpenDropdownID == id) {
            DropdownX = boxX;
            DropdownY = y + height;
            DropdownW = boxW;
            DropdownOptions = options;
            DropdownCount = count;
        }
    }

    bool open = OpenDropdownID == id;

    if (*visible)
        UIDraw::TextClipped(x, y + (height - UIDraw::LineHeight()) / 2.0f, visible,
            UI_COL_TEXT_DIM, UIDraw::Scale, labelWidth - UICore::Pad());

    UIDraw::BeveledRect(boxX, y, boxW, height, hovered || open ? UI_COL_BUTTON_HOVER : UI_COL_BUTTON,
        UI_COL_BORDER_LIGHT, UI_COL_BORDER);

    const char* shown = (*current >= 0 && *current < count) ? options[*current] : "";
    float arrowWidth = UIDraw::CharWidth() * 2.0f;

    UIDraw::TextClipped(boxX + UICore::Pad(), y + (height - UIDraw::LineHeight()) / 2.0f,
        shown, UI_COL_TEXT, UIDraw::Scale, boxW - arrowWidth - UICore::Pad() * 2.0f);

    UIDraw::Text(boxX + boxW - arrowWidth, y + (height - UIDraw::LineHeight()) / 2.0f,
        open ? "^" : "v", UI_COL_TEXT_DIM);

    return changed;
}

// Registers hover help for the widget placed just before this call. The text is
// read again when the overlay is painted, so pass something that outlives the
// frame.
PUBLIC STATIC void UICore::Tooltip(const char* text) {
    if (LastItemHovered)
        PendingTooltip = text;
}

PUBLIC STATIC bool UICore::IsDropdownOpen() {
    return OpenDropdownID != 0;
}

PUBLIC STATIC void UICore::CloseDropdown() {
    OpenDropdownID = 0;
}

// Paints the deferred dropdown list and tooltip, on top of everything else.
// Must be called with no panel open and no clip rectangle in force.
PUBLIC STATIC void UICore::DrawOverlays() {
    if (OpenDropdownID && DropdownOptions && DropdownCount > 0) {
        float rowHeight = UIDraw::LineHeight() + 4.0f * UIDraw::Scale;
        float listHeight = DropdownCount * rowHeight + 2.0f;

        // Flip the list above the box when there is no room below it.
        float listY = DropdownY;
        if (listY + listHeight > UIDraw::ViewHeight)
            listY = DropdownY - UICore::RowHeight() - listHeight;

        UIDraw::FillRect(DropdownX, listY, DropdownW, listHeight, UI_COL_PANEL);
        UIDraw::StrokeRect(DropdownX, listY, DropdownW, listHeight, UI_COL_ACCENT);

        bool insideList = UICore::MouseX >= DropdownX && UICore::MouseX < DropdownX + DropdownW &&
            UICore::MouseY >= listY && UICore::MouseY < listY + listHeight;

        for (int i = 0; i < DropdownCount; i++) {
            float rowY = listY + 1.0f + i * rowHeight;

            char rowID[64];
            snprintf(rowID, sizeof(rowID), "##dropdownrow%d", i);

            bool hovered = false, held = false;
            if (UICore::ClickableRegion(UICore::Hash(rowID, OpenDropdownID),
                    DropdownX + 1.0f, rowY, DropdownW - 2.0f, rowHeight, &hovered, &held)) {
                DropdownResultID = OpenDropdownID;
                DropdownResultIndex = i;
                OpenDropdownID = 0;
            }

            if (hovered)
                UIDraw::FillRect(DropdownX + 1.0f, rowY, DropdownW - 2.0f, rowHeight, UI_COL_SELECTION);

            UIDraw::TextClipped(DropdownX + UICore::Pad(), rowY + 2.0f * UIDraw::Scale,
                DropdownOptions[i], UI_COL_TEXT, UIDraw::Scale, DropdownW - UICore::Pad() * 2.0f);
        }

        // Clicking anywhere else dismisses the list -- but not on the frame
        // the list appeared, since a quick click delivers its press and its
        // release together and that press is the one that opened it.
        if (OpenDropdownID && UICore::MouseWasPressed && !insideList && !DropdownJustOpened)
            OpenDropdownID = 0;
    }

    DropdownJustOpened = false;

    if (PendingTooltip && *PendingTooltip) {
        float textWidth = UIDraw::TextWidth(PendingTooltip);
        float boxW = textWidth + UICore::Pad() * 4.0f;
        float boxH = UIDraw::LineHeight() + UICore::Pad() * 2.0f;

        float boxX = UICore::MouseX + UICore::Pad() * 3.0f;
        float boxY = UICore::MouseY + UICore::Pad() * 3.0f;

        // Keep it on screen when the pointer is near an edge.
        if (boxX + boxW > UIDraw::ViewWidth)
            boxX = UIDraw::ViewWidth - boxW;
        if (boxY + boxH > UIDraw::ViewHeight)
            boxY = UICore::MouseY - boxH - UICore::Pad();
        if (boxX < 0.0f)
            boxX = 0.0f;

        UIDraw::FillRect(boxX, boxY, boxW, boxH, UI_COL_PANEL_HEADER);
        UIDraw::StrokeRect(boxX, boxY, boxW, boxH, UI_COL_BORDER_LIGHT);
        UIDraw::Text(boxX + UICore::Pad() * 2.0f, boxY + UICore::Pad(), PendingTooltip, UI_COL_TEXT);

        PendingTooltip = NULL;
    }
}

// Dims the whole window so a panel drawn on top reads as the focus.
PUBLIC STATIC void UICore::Backdrop() {
    UIDraw::FillRect(0.0f, 0.0f, (float)UIDraw::ViewWidth, (float)UIDraw::ViewHeight, UI_COL_BACKDROP);
}
