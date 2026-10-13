#ifndef ENGINE_UI_UIDRAW_H
#define ENGINE_UI_UIDRAW_H

#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>
#include <Engine/Rendering/Texture.h>
#include <Engine/Scene/View.h>

class UIDraw {
private:
    static void MakeFontTexture();
    static void ApplyClip();
    static float Snap(float value);
    static void UseColor(Uint32 color);

public:
    static bool  Initialized;
    static float Scale;
    static int   ViewWidth;
    static int   ViewHeight;

    static void Init();
    static void Dispose();
    static float GetAutoScale(int windowHeight);
    static void Begin();
    static bool IsInFrame();
    static void End();
    static void PushClip(float x, float y, float w, float h);
    static void PopClip();
    static bool IsClipped(float x, float y, float w, float h);
    static void FillRect(float x, float y, float w, float h, Uint32 color);
    static void StrokeRect(float x, float y, float w, float h, Uint32 color);
    static void BeveledRect(float x, float y, float w, float h, Uint32 fill, Uint32 light, Uint32 dark);
    static float CharWidth();
    static float LineHeight();
    static float TextWidth(const char* text, float scale);
    static float TextWidth(const char* text);
    static float Text(float x, float y, const char* text, Uint32 color, float scale);
    static float Text(float x, float y, const char* text, Uint32 color);
    static void TextCentered(float centerX, float y, const char* text, Uint32 color, float scale);
    static void TextRight(float rightX, float y, const char* text, Uint32 color, float scale);
    static void TextClipped(float x, float y, const char* text, Uint32 color, float scale, float maxWidth);
};

#endif /* ENGINE_UI_UIDRAW_H */
