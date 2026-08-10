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
// on press, and reports a click when the mouse comes back up over it.
PRIVATE STATIC bool UICore::ButtonBehavior(Uint32 id, float x, float y, float w, float h, bool* outHovered, bool* outHeld) {
    bool hovered = UICore::IsOver(x, y, w, h);
    if (hovered)
        HoveredThisFrame = id;

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
        UICore::ButtonBehavior(id, barX, ContentTop, barW, viewHeight, &hovered, &held);

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
    bool clicked = UICore::ButtonBehavior(id, x, y, width, height, &hovered, &held);

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
    bool clicked = UICore::ButtonBehavior(id, x, y, width, height, &hovered, &held);

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
    UICore::ButtonBehavior(id, trackX, y, trackW, height, &hovered, &held);

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
    bool clicked = UICore::ButtonBehavior(id, x, y, width, height, &hovered, &held);

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
            length = (int)strlen(buffer);

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
        bool clicked = UICore::ButtonBehavior(UICore::Hash(names[i], seed), tabX, y, tabWidth, height, &hovered, &held);

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

// Dims the whole window so a panel drawn on top reads as the focus.
PUBLIC STATIC void UICore::Backdrop() {
    UIDraw::FillRect(0.0f, 0.0f, (float)UIDraw::ViewWidth, (float)UIDraw::ViewHeight, UI_COL_BACKDROP);
}
