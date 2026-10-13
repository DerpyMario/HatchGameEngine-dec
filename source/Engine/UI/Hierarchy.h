#ifndef ENGINE_UI_HIERARCHY_H
#define ENGINE_UI_HIERARCHY_H

#include <Engine/Includes/Standard.h>

class Hierarchy {
private:
    static bool Matches(const char* name);

public:
    static void Draw();
};

#endif /* ENGINE_UI_HIERARCHY_H */
