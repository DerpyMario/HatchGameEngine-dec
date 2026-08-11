#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>

class UIMenu {
};
#endif

#include <Engine/UI/UIMenu.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UITheme.h>

// A menu bar in the shape of the one HatchStudio uses: a strip of titles along
// the top, each dropping a list of items with their keyboard shortcuts spelled
// out on the right, and support for separators, tick boxes, radio groups,
// disabled entries and one level of submenu.
//
// Because a dropdown hangs over whatever is beneath it, the whole bar is drawn
// after the rest of the interface. While a menu is open the caller blocks input
// to everything else, so a click on an item cannot also land on the panel
// behind it.

// Menus are static enough that measuring a dropdown as it is drawn and using
// that width on the next frame is invisible in practice, and it saves having to
// declare the contents twice.
struct UIMenuMetrics {
    float Width = 0.0f;
    float Measured = 0.0f;
};

static std::map<Uint32, UIMenuMetrics> MenuMetrics;

static Uint32 OpenMenu = 0;
static Uint32 OpenSubmenu = 0;

// The bar being built.
static bool  InBar = false;
static float BarX = 0.0f;
static float BarY = 0.0f;
static float BarWidth = 0.0f;
static float BarHeight = 0.0f;
static float BarCursorX = 0.0f;
static bool  ClickedInsideMenu = false;

// The dropdown being built.
static bool   InMenu = false;
static Uint32 CurrentMenu = 0;
static float  MenuX = 0.0f;
static float  MenuY = 0.0f;
static float  MenuWidth = 0.0f;
static float  MenuCursorY = 0.0f;
static float  MenuMeasured = 0.0f;

// The submenu being built, which hangs off the item that opened it.
static bool   InSubmenu = false;
static Uint32 CurrentSubmenu = 0;
static float  SubmenuX = 0.0f;
static float  SubmenuY = 0.0f;
static float  SubmenuWidth = 0.0f;
static float  SubmenuCursorY = 0.0f;
static float  SubmenuMeasured = 0.0f;

PUBLIC STATIC bool UIMenu::IsOpen() {
    return OpenMenu != 0;
}

PUBLIC STATIC void UIMenu::Close() {
    OpenMenu = 0;
    OpenSubmenu = 0;
}

PRIVATE STATIC float UIMenu::RowHeight() {
    return UIDraw::LineHeight() + 5.0f * UIDraw::Scale;
}

// How wide an entry wants to be, with room for its tick column and shortcut.
PRIVATE STATIC float UIMenu::MeasureItem(const char* label, const char* shortcut) {
    float width = UIDraw::TextWidth(label) + UIDraw::CharWidth() * 4.0f;

    if (shortcut && *shortcut)
        width += UIDraw::TextWidth(shortcut) + UIDraw::CharWidth() * 3.0f;

    return width + UICore::Pad() * 4.0f;
}

// ------------------------------------------------------------------- bar ----

PUBLIC STATIC void UIMenu::Begin(float x, float y, float width, float height) {
    InBar = true;
    BarX = x;
    BarY = y;
    BarWidth = width;
    BarHeight = height;
    BarCursorX = x + UICore::Pad();
    ClickedInsideMenu = false;

    UIDraw::FillRect(x, y, width, height, UI_COL_PANEL_HEADER);
    UIDraw::FillRect(x, y + height - 1.0f, width, 1.0f, UI_COL_BORDER);
}

PUBLIC STATIC void UIMenu::End() {
    // A press that missed every title and every open item closes the menu,
    // which is what clicking away from a menu is expected to do.
    if (OpenMenu && UICore::MouseWasPressed && !ClickedInsideMenu)
        UIMenu::Close();

    InBar = false;
}

// Opens the named menu and returns true while its contents should be declared.
PUBLIC STATIC bool UIMenu::BeginMenu(const char* label) {
    if (!InBar)
        return false;

    Uint32 id = UICore::MakeID(label);

    float titleWidth = UIDraw::TextWidth(label) + UICore::Pad() * 4.0f;
    float titleX = BarCursorX;
    BarCursorX += titleWidth;

    bool hovered = false, held = false;
    bool clicked = UICore::ClickableRegion(id, titleX, BarY, titleWidth, BarHeight, &hovered, &held);

    if (hovered && UICore::MouseWasPressed)
        ClickedInsideMenu = true;

    if (clicked) {
        OpenMenu = OpenMenu == id ? 0 : id;
        OpenSubmenu = 0;
    }
    // Once one menu is open, sliding across the bar switches between them.
    else if (OpenMenu && OpenMenu != id && hovered) {
        OpenMenu = id;
        OpenSubmenu = 0;
    }

    bool open = OpenMenu == id;

    if (open)
        UIDraw::FillRect(titleX, BarY, titleWidth, BarHeight - 1.0f, UI_COL_ACCENT_DIM);
    else if (hovered)
        UIDraw::FillRect(titleX, BarY, titleWidth, BarHeight - 1.0f, UI_COL_BUTTON);

    UIDraw::TextCentered(titleX + titleWidth / 2.0f, BarY + (BarHeight - UIDraw::LineHeight()) / 2.0f,
        label, open || hovered ? UI_COL_TEXT : UI_COL_TEXT_DIM, UIDraw::Scale);

    if (!open)
        return false;

    UIMenuMetrics& metrics = MenuMetrics[id];

    InMenu = true;
    CurrentMenu = id;
    MenuX = titleX;
    MenuY = BarY + BarHeight;
    MenuWidth = metrics.Width > 0.0f ? metrics.Width : UIDraw::CharWidth() * 24.0f;
    MenuCursorY = MenuY + UICore::Pad();
    MenuMeasured = 0.0f;

    // Keep the dropdown on screen when a menu near the right edge is opened.
    if (MenuX + MenuWidth > UIDraw::ViewWidth)
        MenuX = UIDraw::ViewWidth - MenuWidth;
    if (MenuX < 0.0f)
        MenuX = 0.0f;

    return true;
}

PUBLIC STATIC void UIMenu::EndMenu() {
    if (!InMenu)
        return;

    if (InSubmenu)
        UIMenu::EndSubmenu();

    float height = MenuCursorY - MenuY + UICore::Pad();

    // Each row paints its own background, which leaves the margins above the
    // first one and below the last one showing whatever the dropdown covers.
    UIDraw::FillRect(MenuX + 1.0f, MenuY, MenuWidth - 2.0f, UICore::Pad(), UI_COL_PANEL);
    UIDraw::FillRect(MenuX + 1.0f, MenuCursorY, MenuWidth - 2.0f, UICore::Pad(), UI_COL_PANEL);

    // The frame is painted after the items, so it sits over their edges rather
    // than being covered by them.
    UIDraw::StrokeRect(MenuX, MenuY, MenuWidth, height, UI_COL_ACCENT);

    MenuMetrics[CurrentMenu].Width = MenuMeasured;

    InMenu = false;
    CurrentMenu = 0;
}

// ----------------------------------------------------------------- items ----

PRIVATE STATIC void UIMenu::TrackWidth(float width) {
    if (InSubmenu) {
        if (width > SubmenuMeasured)
            SubmenuMeasured = width;
    }
    else if (width > MenuMeasured)
        MenuMeasured = width;
}

// Reserves the next row of whichever list is being built.
PRIVATE STATIC void UIMenu::PlaceRow(float height, float* outX, float* outY, float* outWidth) {
    if (InSubmenu) {
        *outX = SubmenuX;
        *outY = SubmenuCursorY;
        *outWidth = SubmenuWidth;
        SubmenuCursorY += height;
    }
    else {
        *outX = MenuX;
        *outY = MenuCursorY;
        *outWidth = MenuWidth;
        MenuCursorY += height;
    }
}

// Shared drawing and hit testing for every kind of entry. `mark` is the glyph
// shown in the column on the left, for ticks and radio dots.
PRIVATE STATIC bool UIMenu::DrawRow(const char* label, const char* shortcut, const char* mark, bool enabled, bool submenuArrow) {
    if (!InMenu)
        return false;

    UIMenu::TrackWidth(UIMenu::MeasureItem(label, shortcut));

    float height = UIMenu::RowHeight();
    float x, y, width;
    UIMenu::PlaceRow(height, &x, &y, &width);

    // The background is painted first so the item's own frame stays visible.
    UIDraw::FillRect(x + 1.0f, y, width - 2.0f, height, UI_COL_PANEL);

    bool hovered = false, held = false;
    bool clicked = false;

    if (enabled) {
        char rowID[320];
        snprintf(rowID, sizeof(rowID), "%s##menurow", label);

        clicked = UICore::ClickableRegion(UICore::MakeID(rowID, InSubmenu ? CurrentSubmenu : CurrentMenu),
            x + 1.0f, y, width - 2.0f, height, &hovered, &held);

        if (hovered && UICore::MouseWasPressed)
            ClickedInsideMenu = true;

        // Moving onto a plain row folds away whichever submenu was open,
        // without disturbing rows that belong to the submenu itself.
        if (hovered && !InSubmenu)
            OpenSubmenu = 0;

        if (hovered)
            UIDraw::FillRect(x + 1.0f, y, width - 2.0f, height, UI_COL_SELECTION);
    }

    float markWidth = UIDraw::CharWidth() * 2.0f;
    float textY = y + (height - UIDraw::LineHeight()) / 2.0f;

    if (mark && *mark)
        UIDraw::Text(x + UICore::Pad(), textY, mark, enabled ? UI_COL_ACCENT : UI_COL_TEXT_FAINT);

    UIDraw::TextClipped(x + UICore::Pad() + markWidth, textY, label,
        enabled ? UI_COL_TEXT : UI_COL_TEXT_FAINT, UIDraw::Scale, width - markWidth - UICore::Pad() * 3.0f);

    if (shortcut && *shortcut)
        UIDraw::TextRight(x + width - UICore::Pad() * 2.0f, textY, shortcut,
            enabled ? UI_COL_TEXT_FAINT : UI_COL_BORDER_LIGHT, UIDraw::Scale);
    else if (submenuArrow)
        UIDraw::TextRight(x + width - UICore::Pad() * 2.0f, textY, ">",
            enabled ? UI_COL_TEXT_DIM : UI_COL_TEXT_FAINT, UIDraw::Scale);

    if (clicked)
        UIMenu::Close();

    return clicked;
}

PUBLIC STATIC bool UIMenu::Item(const char* label, const char* shortcut, bool enabled) {
    return UIMenu::DrawRow(label, shortcut, NULL, enabled, false);
}

PUBLIC STATIC bool UIMenu::CheckItem(const char* label, const char* shortcut, bool checked, bool enabled) {
    return UIMenu::DrawRow(label, shortcut, checked ? "x" : " ", enabled, false);
}

PUBLIC STATIC bool UIMenu::RadioItem(const char* label, bool selected, bool enabled) {
    return UIMenu::DrawRow(label, NULL, selected ? "*" : " ", enabled, false);
}

PUBLIC STATIC void UIMenu::Separator() {
    if (!InMenu)
        return;

    float height = UICore::Pad() * 2.0f;
    float x, y, width;
    UIMenu::PlaceRow(height, &x, &y, &width);

    UIDraw::FillRect(x + 1.0f, y, width - 2.0f, height, UI_COL_PANEL);
    UIDraw::FillRect(x + UICore::Pad(), y + height / 2.0f, width - UICore::Pad() * 2.0f, 1.0f, UI_COL_BORDER);
}

// A row that opens a second list beside it. Returns true while that list's
// contents should be declared.
PUBLIC STATIC bool UIMenu::BeginSubmenu(const char* label) {
    if (!InMenu || InSubmenu)
        return false;

    Uint32 id = UICore::MakeID(label, CurrentMenu);

    float height = UIMenu::RowHeight();
    float rowY = MenuCursorY;

    UIMenu::TrackWidth(UIMenu::MeasureItem(label, "    "));

    float x, y, width;
    UIMenu::PlaceRow(height, &x, &y, &width);

    UIDraw::FillRect(x + 1.0f, y, width - 2.0f, height, UI_COL_PANEL);

    bool hovered = false, held = false;
    UICore::ClickableRegion(id, x + 1.0f, y, width - 2.0f, height, &hovered, &held);

    // Hovering a submenu row opens it and closes any sibling that was open.
    if (hovered) {
        OpenSubmenu = id;
        if (UICore::MouseWasPressed)
            ClickedInsideMenu = true;
    }

    bool open = OpenSubmenu == id;

    if (hovered || open)
        UIDraw::FillRect(x + 1.0f, y, width - 2.0f, height, UI_COL_SELECTION);

    float markWidth = UIDraw::CharWidth() * 2.0f;
    float textY = y + (height - UIDraw::LineHeight()) / 2.0f;

    UIDraw::TextClipped(x + UICore::Pad() + markWidth, textY, label, UI_COL_TEXT,
        UIDraw::Scale, width - markWidth - UICore::Pad() * 3.0f);
    UIDraw::TextRight(x + width - UICore::Pad() * 2.0f, textY, ">", UI_COL_TEXT_DIM, UIDraw::Scale);

    if (!open)
        return false;

    UIMenuMetrics& metrics = MenuMetrics[id];

    InSubmenu = true;
    CurrentSubmenu = id;
    SubmenuWidth = metrics.Width > 0.0f ? metrics.Width : UIDraw::CharWidth() * 24.0f;
    SubmenuX = MenuX + MenuWidth - 1.0f;
    SubmenuY = rowY;
    SubmenuCursorY = SubmenuY + UICore::Pad();
    SubmenuMeasured = 0.0f;

    // Fold it back over the parent when there is no room to the right.
    if (SubmenuX + SubmenuWidth > UIDraw::ViewWidth)
        SubmenuX = MenuX - SubmenuWidth + 1.0f;
    if (SubmenuX < 0.0f)
        SubmenuX = 0.0f;

    return true;
}

PUBLIC STATIC void UIMenu::EndSubmenu() {
    if (!InSubmenu)
        return;

    float height = SubmenuCursorY - SubmenuY + UICore::Pad();

    UIDraw::FillRect(SubmenuX + 1.0f, SubmenuY, SubmenuWidth - 2.0f, UICore::Pad(), UI_COL_PANEL);
    UIDraw::FillRect(SubmenuX + 1.0f, SubmenuCursorY, SubmenuWidth - 2.0f, UICore::Pad(), UI_COL_PANEL);

    UIDraw::StrokeRect(SubmenuX, SubmenuY, SubmenuWidth, height, UI_COL_ACCENT);

    MenuMetrics[CurrentSubmenu].Width = SubmenuMeasured;

    // A press inside the submenu belongs to the menu, not to what is behind it.
    if (UICore::MouseWasPressed &&
        UICore::MouseX >= SubmenuX && UICore::MouseX < SubmenuX + SubmenuWidth &&
        UICore::MouseY >= SubmenuY && UICore::MouseY < SubmenuY + height)
        ClickedInsideMenu = true;

    InSubmenu = false;
    CurrentSubmenu = 0;
}
