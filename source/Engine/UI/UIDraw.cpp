#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>
#include <Engine/Rendering/Texture.h>
#include <Engine/Scene/View.h>

class UIDraw {
public:
    static bool  Initialized;
    static float Scale;
    static int   ViewWidth;
    static int   ViewHeight;
};
#endif

#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UIFontData.h>

#include <Engine/Application.h>
#include <Engine/Graphics.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Rendering/Enums.h>
#include <Engine/Scene.h>

bool  UIDraw::Initialized = false;
float UIDraw::Scale = 2.0f;
int   UIDraw::ViewWidth = 0;
int   UIDraw::ViewHeight = 0;

struct UIClipRect {
    int X;
    int Y;
    int Width;
    int Height;
};

// The editor draws through the engine's own renderer, which means it needs the
// same bits of global state that Scene::RenderView sets up. These hold what was
// there before UIDraw::Begin so it can all be put back in UIDraw::End.
static Texture*   FontTexture = NULL;
static View       UIView;
static View*      BackupView = NULL;
static int        BackupViewCurrent = 0;
static bool       BackupTextureBlend = false;
static bool       BackupTextureInterpolate = false;
static bool       InFrame = false;

static vector<UIClipRect> ClipStack;

// Builds the font atlas described by UIFontData.h: one 6x8 cell per glyph laid
// out in a 16x6 grid, white pixels where the glyph is set and transparent
// everywhere else. Tinting is done at draw time through the blend color.
PRIVATE STATIC void UIDraw::MakeFontTexture() {
    const int atlasW = UI_FONT_COLUMNS * UI_FONT_CELL_W;
    const int atlasH = UI_FONT_ROWS * UI_FONT_CELL_H;

    Uint32* pixels = (Uint32*)calloc(atlasW * atlasH, sizeof(Uint32));
    if (!pixels)
        return;

    for (int glyph = 0; glyph < UI_FONT_CHAR_COUNT; glyph++) {
        const char* rows = UI_FontGlyphs[glyph];
        int originX = (glyph % UI_FONT_COLUMNS) * UI_FONT_CELL_W;
        int originY = (glyph / UI_FONT_COLUMNS) * UI_FONT_CELL_H;

        for (int y = 0; y < UI_FONT_GLYPH_H; y++) {
            for (int x = 0; x < UI_FONT_GLYPH_W; x++) {
                if (rows[y * UI_FONT_GLYPH_W + x] != '#')
                    continue;

                pixels[(originY + y) * atlasW + (originX + x)] = 0xFFFFFFFFU;
            }
        }
    }

    bool interpolate = Graphics::TextureInterpolate;
    Graphics::SetTextureInterpolation(false);

    FontTexture = Graphics::CreateTextureFromPixels(atlasW, atlasH, pixels, atlasW * sizeof(Uint32));

    Graphics::SetTextureInterpolation(interpolate);

    free(pixels);

    if (!FontTexture)
        Log::Print(Log::LOG_ERROR, "Could not create the interface font texture!");
}

PUBLIC STATIC void UIDraw::Init() {
    if (UIDraw::Initialized)
        return;

    UIDraw::MakeFontTexture();

    UIView.Active = true;
    UIView.Visible = true;
    UIView.Software = false;
    UIView.UseDrawTarget = false;
    UIView.UsePerspective = false;
    UIView.X = 0.0f;
    UIView.Y = 0.0f;
    UIView.Z = 0.0f;
    UIView.Width = 1.0f;
    UIView.Height = 1.0f;

    UIDraw::Initialized = true;
}

PUBLIC STATIC void UIDraw::Dispose() {
    if (FontTexture) {
        Graphics::DisposeTexture(FontTexture);
        FontTexture = NULL;
    }

    ClipStack.clear();

    UIDraw::Initialized = false;
}

// Picks an interface scale that keeps text readable on both a small window and
// a 4K display.
PUBLIC STATIC float UIDraw::GetAutoScale(int windowHeight) {
    if (windowHeight >= 1400)
        return 3.0f;
    if (windowHeight >= 700)
        return 2.0f;
    return 1.0f;
}

PUBLIC STATIC void UIDraw::Begin() {
    if (InFrame)
        return;

    int windowW = 0, windowH = 0;
    SDL_GetWindowSize(Application::Window, &windowW, &windowH);
    if (windowW <= 0 || windowH <= 0)
        return;

    // The renderers project through the current scene view's matrix, so there
    // is nothing to draw into until Scene::Init has created one.
    if (!Scene::Views[Scene::ViewCurrent].ProjectionMatrix)
        return;

    UIDraw::ViewWidth = windowW;
    UIDraw::ViewHeight = windowH;
    UIDraw::Scale = UIDraw::GetAutoScale(windowH);

    // The renderers read the window-space size off the current view (for the
    // scissor rectangle in particular), so point them at a view that covers
    // the whole window one-to-one.
    UIView.Width = (float)windowW;
    UIView.Height = (float)windowH;
    UIView.OutputWidth = (float)windowW;
    UIView.OutputHeight = (float)windowH;
    UIView.ProjectionMatrix = Scene::Views[Scene::ViewCurrent].ProjectionMatrix;
    UIView.BaseProjectionMatrix = Scene::Views[Scene::ViewCurrent].BaseProjectionMatrix;

    BackupView = Graphics::CurrentView;
    BackupViewCurrent = Scene::ViewCurrent;
    BackupTextureBlend = Graphics::TextureBlend;
    BackupTextureInterpolate = Graphics::TextureInterpolate;

    Graphics::CurrentView = &UIView;

    Graphics::SetViewport(0.0f, 0.0f, (float)windowW, (float)windowH);
    Graphics::ClearClip();
    Graphics::UpdateOrthoFlipped((float)windowW, (float)windowH);
    Graphics::UpdateProjectionMatrix();

    Graphics::SetDepthTesting(false);
    Graphics::SetTintEnabled(false);
    Graphics::SetTextureInterpolation(false);
    Graphics::SetBlendMode(BlendFactor_SRC_ALPHA, BlendFactor_INV_SRC_ALPHA,
        BlendFactor_SRC_ALPHA, BlendFactor_INV_SRC_ALPHA);

    // Without this the renderers ignore the blend color when drawing textures,
    // which would leave every glyph plain white.
    Graphics::TextureBlend = true;

    Graphics::Save();

    InFrame = true;
}

// False when Begin declined to start a frame, which means nothing may be drawn.
PUBLIC STATIC bool UIDraw::IsInFrame() {
    return InFrame;
}

PUBLIC STATIC void UIDraw::End() {
    if (!InFrame)
        return;

    while (ClipStack.size())
        UIDraw::PopClip();

    Graphics::Restore();

    Graphics::SetBlendColor(1.0f, 1.0f, 1.0f, 1.0f);
    Graphics::TextureBlend = BackupTextureBlend;
    Graphics::SetTextureInterpolation(BackupTextureInterpolate);

    Graphics::CurrentView = BackupView;
    Scene::ViewCurrent = BackupViewCurrent;

    InFrame = false;
}

PRIVATE STATIC void UIDraw::ApplyClip() {
    if (!ClipStack.size()) {
        Graphics::ClearClip();
        return;
    }

    UIClipRect& clip = ClipStack.back();
    Graphics::SetClip(clip.X, clip.Y, clip.Width, clip.Height);
}

PUBLIC STATIC void UIDraw::PushClip(float x, float y, float w, float h) {
    UIClipRect clip;
    clip.X = (int)x;
    clip.Y = (int)y;
    clip.Width = (int)w;
    clip.Height = (int)h;

    // Nested clips have to stay inside their parent, otherwise a scrolling list
    // could paint over the panel that contains it.
    if (ClipStack.size()) {
        UIClipRect& parent = ClipStack.back();
        int x1 = std::max(clip.X, parent.X);
        int y1 = std::max(clip.Y, parent.Y);
        int x2 = std::min(clip.X + clip.Width, parent.X + parent.Width);
        int y2 = std::min(clip.Y + clip.Height, parent.Y + parent.Height);
        clip.X = x1;
        clip.Y = y1;
        clip.Width = std::max(0, x2 - x1);
        clip.Height = std::max(0, y2 - y1);
    }

    ClipStack.push_back(clip);
    UIDraw::ApplyClip();
}

PUBLIC STATIC void UIDraw::PopClip() {
    if (!ClipStack.size())
        return;

    ClipStack.pop_back();
    UIDraw::ApplyClip();
}

PUBLIC STATIC bool UIDraw::IsClipped(float x, float y, float w, float h) {
    if (!ClipStack.size())
        return false;

    UIClipRect& clip = ClipStack.back();
    return x + w < clip.X || y + h < clip.Y ||
        x > clip.X + clip.Width || y > clip.Y + clip.Height;
}

// Centering and right-aligning produce fractional coordinates, and a quad that
// starts half a pixel in covers half a pixel less than it should -- enough to
// drop the last column of a five pixel wide glyph. Everything the interface
// draws is snapped to whole pixels to keep the text crisp.
PRIVATE STATIC float UIDraw::Snap(float value) {
    return floorf(value + 0.5f);
}

PRIVATE STATIC void UIDraw::UseColor(Uint32 color) {
    Graphics::SetBlendColor(
        ((color >> 16) & 0xFF) / 255.0f,
        ((color >> 8) & 0xFF) / 255.0f,
        (color & 0xFF) / 255.0f,
        ((color >> 24) & 0xFF) / 255.0f);
}

PUBLIC STATIC void UIDraw::FillRect(float x, float y, float w, float h, Uint32 color) {
    if (w <= 0.0f || h <= 0.0f || !(color & 0xFF000000U))
        return;
    if (UIDraw::IsClipped(x, y, w, h))
        return;

    float left = UIDraw::Snap(x);
    float top = UIDraw::Snap(y);
    float right = UIDraw::Snap(x + w);
    float bottom = UIDraw::Snap(y + h);

    // Snapping both edges can collapse a hairline to nothing; keep it visible.
    if (right <= left)
        right = left + 1.0f;
    if (bottom <= top)
        bottom = top + 1.0f;

    UIDraw::UseColor(color);
    Graphics::FillRectangle(left, top, right - left, bottom - top);
}

PUBLIC STATIC void UIDraw::StrokeRect(float x, float y, float w, float h, Uint32 color) {
    if (w <= 0.0f || h <= 0.0f)
        return;

    UIDraw::FillRect(x, y, w, 1.0f, color);
    UIDraw::FillRect(x, y + h - 1.0f, w, 1.0f, color);
    UIDraw::FillRect(x, y + 1.0f, 1.0f, h - 2.0f, color);
    UIDraw::FillRect(x + w - 1.0f, y + 1.0f, 1.0f, h - 2.0f, color);
}

// Draws a rectangle with a lighter top edge and a darker bottom edge, which is
// what gives buttons and panels their raised look.
PUBLIC STATIC void UIDraw::BeveledRect(float x, float y, float w, float h, Uint32 fill, Uint32 light, Uint32 dark) {
    UIDraw::FillRect(x, y, w, h, fill);
    UIDraw::FillRect(x, y, w, 1.0f, light);
    UIDraw::FillRect(x, y, 1.0f, h, light);
    UIDraw::FillRect(x, y + h - 1.0f, w, 1.0f, dark);
    UIDraw::FillRect(x + w - 1.0f, y, 1.0f, h, dark);
}

PUBLIC STATIC float UIDraw::CharWidth() {
    return UI_FONT_CELL_W * UIDraw::Scale;
}

PUBLIC STATIC float UIDraw::LineHeight() {
    return UI_FONT_CELL_H * UIDraw::Scale;
}

PUBLIC STATIC float UIDraw::TextWidth(const char* text, float scale) {
    if (!text)
        return 0.0f;

    size_t length = strlen(text);
    if (!length)
        return 0.0f;

    // The trailing column of the last cell is spacing, not part of the glyph.
    return (length * UI_FONT_CELL_W - 1) * scale;
}

PUBLIC STATIC float UIDraw::TextWidth(const char* text) {
    return UIDraw::TextWidth(text, UIDraw::Scale);
}

PUBLIC STATIC float UIDraw::Text(float x, float y, const char* text, Uint32 color, float scale) {
    if (!text || !FontTexture)
        return x;

    float glyphW = UI_FONT_GLYPH_W * scale;
    float glyphH = UI_FONT_GLYPH_H * scale;
    float advance = UI_FONT_CELL_W * scale;

    if (UIDraw::IsClipped(x, y, UIDraw::TextWidth(text, scale), glyphH))
        return x + UIDraw::TextWidth(text, scale);

    UIDraw::UseColor(color);

    float penX = UIDraw::Snap(x);
    float penY = UIDraw::Snap(y);

    for (const char* i = text; *i; i++, penX += advance) {
        if (*i == ' ')
            continue;

        int glyph = (unsigned char)*i - UI_FONT_FIRST_CHAR;
        if (glyph < 0 || glyph >= UI_FONT_CHAR_COUNT)
            glyph = UI_FONT_CHAR_COUNT - 1;

        float sx = (float)((glyph % UI_FONT_COLUMNS) * UI_FONT_CELL_W);
        float sy = (float)((glyph / UI_FONT_COLUMNS) * UI_FONT_CELL_H);

        Graphics::DrawTexture(FontTexture,
            sx, sy, (float)UI_FONT_GLYPH_W, (float)UI_FONT_GLYPH_H,
            penX, penY, glyphW, glyphH);
    }

    return penX;
}

PUBLIC STATIC float UIDraw::Text(float x, float y, const char* text, Uint32 color) {
    return UIDraw::Text(x, y, text, color, UIDraw::Scale);
}

PUBLIC STATIC void UIDraw::TextCentered(float centerX, float y, const char* text, Uint32 color, float scale) {
    UIDraw::Text(centerX - UIDraw::TextWidth(text, scale) / 2.0f, y, text, color, scale);
}

PUBLIC STATIC void UIDraw::TextRight(float rightX, float y, const char* text, Uint32 color, float scale) {
    UIDraw::Text(rightX - UIDraw::TextWidth(text, scale), y, text, color, scale);
}

// Draws text cut off at maxWidth, ending in an ellipsis when it doesn't fit.
// Long file paths and scene names are common enough here to be worth it.
PUBLIC STATIC void UIDraw::TextClipped(float x, float y, const char* text, Uint32 color, float scale, float maxWidth) {
    if (!text)
        return;

    if (UIDraw::TextWidth(text, scale) <= maxWidth) {
        UIDraw::Text(x, y, text, color, scale);
        return;
    }

    int maxChars = (int)(maxWidth / (UI_FONT_CELL_W * scale));
    if (maxChars <= 3) {
        if (maxChars > 0) {
            char stub[4] = { 0 };
            memcpy(stub, text, maxChars);
            UIDraw::Text(x, y, stub, color, scale);
        }
        return;
    }

    char buffer[512];
    int copy = maxChars - 3;
    if (copy > (int)sizeof(buffer) - 4)
        copy = (int)sizeof(buffer) - 4;

    memcpy(buffer, text, copy);
    buffer[copy] = buffer[copy + 1] = buffer[copy + 2] = '.';
    buffer[copy + 3] = '\0';

    UIDraw::Text(x, y, buffer, color, scale);
}
