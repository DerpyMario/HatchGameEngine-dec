#ifndef ENGINE_UI_UIFILEDIALOG_H
#define ENGINE_UI_UIFILEDIALOG_H

#include <Engine/Includes/Standard.h>

class UIFileDialog {
private:
    static void NormalizePath(char* path);
    static void Refresh();
    static void JoinPath(char* out, size_t outSize, const char* base, const char* name);
    static void EnterFolder(const char* name);

public:
    enum Mode {
        PICK_FOLDER, 
        PICK_FILE 
    }; 
    enum Result {
        RESULT_NONE, 
        RESULT_ACCEPTED, 
        RESULT_CANCELLED 
    }; 

    static void Open(const char* title, int mode, const char* startPath, const char* filter);
    static bool IsOpen();
    static void Close();
    static const char* GetSelectedPath();
    static int Draw();
};

#endif /* ENGINE_UI_UIFILEDIALOG_H */
