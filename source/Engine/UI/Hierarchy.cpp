#if INTERFACE
#include <Engine/Includes/Standard.h>

class Hierarchy {
public:

};
#endif

#include <Engine/UI/Hierarchy.h>

#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Types/Entity.h>
#include <Engine/Types/ObjectList.h>
#include <Engine/UI/Selection.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UITheme.h>

// What is in the scene, as a list you can point at.
//
// Unity's hierarchy is a tree of GameObjects. A Hatch scene is not shaped like
// that -- it has layers of tiles, and entities in a flat list belonging to
// object lists -- so this is grouped the way the scene actually is rather than
// pretending otherwise. The part worth taking from Unity is not the tree, it is
// that clicking a row here selects the same thing clicking in the scene view
// selects, and the inspector is looking at whichever it was.

static char Filter[64] = "";

PRIVATE STATIC bool Hierarchy::Matches(const char* name) {
    if (!Filter[0])
        return true;

    if (!name)
        return false;

    // Case-insensitive substring, so "ring" finds "RingObject" without anyone
    // having to remember how it was capitalised.
    for (const char* at = name; *at; at++) {
        const char* a = at;
        const char* b = Filter;

        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');

            if (ca != cb)
                break;

            a++; b++;
        }

        if (!*b)
            return true;
    }

    return false;
}

PUBLIC STATIC void Hierarchy::Draw() {
    if (!Scene::CurrentScene[0]) {
        UICore::Text("No scene is loaded.", UI_COL_TEXT_FAINT);
        UICore::Text("Open one from the Scenes tab.", UI_COL_TEXT_FAINT);
        return;
    }

    UICore::TextField("Filter", Filter, sizeof(Filter));

    UICore::Separator();
    UICore::Heading("Layers");
    UICore::ResetRowStriping();

    if (Scene::Layers.size() == 0)
        UICore::Text("This scene has no layers.", UI_COL_TEXT_FAINT);

    int layersShown = 0;

    for (size_t i = 0; i < Scene::Layers.size(); i++) {
        SceneLayer& layer = Scene::Layers[i];
        const char* name = layer.Name[0] ? layer.Name : "Unnamed";

        if (!Hierarchy::Matches(name))
            continue;

        layersShown++;

        char label[128];
        snprintf(label, sizeof(label), "%s%s##layer%d",
            layer.Visible ? "" : "(hidden) ", name, (int)i);

        if (UICore::ListItem(label, Selection::GetLayerIndex() == (int)i))
            Selection::SetLayer((int)i);
    }

    if (Scene::Layers.size() && !layersShown)
        UICore::Text("No layer matches the filter.", UI_COL_TEXT_FAINT);

    UICore::Separator();

    // Entities are counted rather than just listed: a scene with four thousand
    // of them is a fact worth seeing before scrolling through them. With a
    // filter on, the heading counts what is on screen and what was left out,
    // because a heading saying six over a list of four is just a lie.
    int total = 0;
    int matching = 0;
    for (Entity* entity = Scene::ObjectFirst; entity; entity = entity->NextEntity) {
        total++;

        const char* name = (entity->List && entity->List->ObjectName)
            ? entity->List->ObjectName : "Entity";
        if (Hierarchy::Matches(name))
            matching++;
    }

    char heading[64];
    if (matching == total)
        snprintf(heading, sizeof(heading), "Entities (%d)", total);
    else
        snprintf(heading, sizeof(heading), "Entities (%d of %d)", matching, total);
    UICore::Heading(heading);
    UICore::ResetRowStriping();

    if (!total)
        UICore::Text("Nothing is in the scene yet.", UI_COL_TEXT_FAINT);
    else if (!matching)
        UICore::Text("No entity matches the filter.", UI_COL_TEXT_FAINT);

    // Listing every entity of a busy scene would cost more than the editor is
    // worth, so this stops and says how many it did not draw.
    const int limit = 400;
    int drawn = 0;
    int skipped = 0;

    for (Entity* entity = Scene::ObjectFirst; entity; entity = entity->NextEntity) {
        const char* name = (entity->List && entity->List->ObjectName)
            ? entity->List->ObjectName : "Entity";

        if (!Hierarchy::Matches(name))
            continue;

        if (drawn >= limit) {
            skipped++;
            continue;
        }

        char label[192];
        snprintf(label, sizeof(label), "%s%s  (%d, %d)##ent%p",
            entity->Active ? "" : "(inactive) ",
            name, (int)entity->X, (int)entity->Y, (void*)entity);

        if (UICore::ListItem(label, Selection::GetEntity() == entity))
            Selection::SetEntity(entity);

        drawn++;
    }

    if (skipped) {
        char more[96];
        snprintf(more, sizeof(more), "%d more not listed. Narrow the filter.", skipped);
        UICore::Text(more, UI_COL_TEXT_FAINT);
    }
}
