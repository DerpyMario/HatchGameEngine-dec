#if INTERFACE
#include <Engine/Includes/Standard.h>

need_t Entity;
need_t SceneLayer;

class Selection {
public:
    enum {
        KIND_NONE = 0,
        KIND_ENTITY,
        KIND_LAYER
    };
};
#endif

#include <Engine/UI/Selection.h>

#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Types/Entity.h>
#include <Engine/Types/ObjectList.h>

// What the editor is currently looking at.
//
// Unity keeps this in one place -- its Selection -- rather than letting each
// window own its own idea of what is selected, and that one decision is what
// makes a hierarchy, an inspector and a scene view feel like parts of the same
// program instead of three tools in a row. This is that, for this editor: the
// scene view sets it by clicking, the hierarchy sets it by clicking, and the
// inspector reads it. None of them knows about the others.
//
// The design is Unity's. The code is not: UnityCsReference is published under a
// reference-only licence, so it is a thing to learn the shape from and not a
// thing to copy out of.

static int      Kind = Selection::KIND_NONE;
static Entity*  Entity_ = NULL;
static int      LayerIndex = -1;

// Bumped whenever the selection changes. Unity raises an event; in an immediate
// mode UI there is nothing to subscribe with, so a panel that needs to notice
// -- to reset a scroll position, say -- compares this against what it saw last.
static Uint32   Version_ = 0;

// An entity is a pointer into a list the scene owns and frees. Reloading a
// scene leaves whatever was selected dangling, and an inspector reading through
// it is a crash rather than a wrong number. So nothing is trusted: the pointer
// is looked for in the live list before it is handed out, once a frame.
//
// A layer index survives the same way, except that it goes quietly out of range
// instead of dangling -- load a scene with fewer layers and the index still
// looks like a selection while pointing at nothing. Dropping it here means the
// inspector says nothing is selected rather than drawing an empty panel.
PUBLIC STATIC void Selection::Validate() {
    if (Kind == Selection::KIND_LAYER) {
        if (LayerIndex < 0 || LayerIndex >= (int)Scene::Layers.size())
            Selection::Clear();

        return;
    }

    if (Kind != Selection::KIND_ENTITY)
        return;

    for (Entity* entity = Scene::ObjectFirst; entity; entity = entity->NextEntity) {
        if (entity == Entity_)
            return;
    }

    Selection::Clear();
}

PUBLIC STATIC void Selection::Clear() {
    if (Kind == Selection::KIND_NONE)
        return;

    Kind = Selection::KIND_NONE;
    Entity_ = NULL;
    LayerIndex = -1;
    Version_++;
}

PUBLIC STATIC void Selection::SetEntity(Entity* entity) {
    if (Kind == Selection::KIND_ENTITY && Entity_ == entity)
        return;

    if (!entity) {
        Selection::Clear();
        return;
    }

    Kind = Selection::KIND_ENTITY;
    Entity_ = entity;
    LayerIndex = -1;
    Version_++;
}

PUBLIC STATIC void Selection::SetLayer(int index) {
    if (Kind == Selection::KIND_LAYER && LayerIndex == index)
        return;

    if (index < 0 || index >= (int)Scene::Layers.size()) {
        Selection::Clear();
        return;
    }

    Kind = Selection::KIND_LAYER;
    LayerIndex = index;
    Entity_ = NULL;
    Version_++;
}

PUBLIC STATIC int Selection::GetKind() {
    return Kind;
}

PUBLIC STATIC Entity* Selection::GetEntity() {
    return Kind == Selection::KIND_ENTITY ? Entity_ : NULL;
}

PUBLIC STATIC int Selection::GetLayerIndex() {
    return Kind == Selection::KIND_LAYER ? LayerIndex : -1;
}

PUBLIC STATIC SceneLayer* Selection::GetLayer() {
    if (Kind != Selection::KIND_LAYER)
        return NULL;

    if (LayerIndex < 0 || LayerIndex >= (int)Scene::Layers.size())
        return NULL;

    return &Scene::Layers[LayerIndex];
}

PUBLIC STATIC Uint32 Selection::GetVersion() {
    return Version_;
}

// The name to show for whatever is selected. An entity is named by the object
// list it belongs to, which is the closest thing this engine has to a class
// name; one that belongs to no list is still worth being able to point at.
PUBLIC STATIC void Selection::GetName(char* out, size_t outSize) {
    if (Kind == Selection::KIND_ENTITY && Entity_) {
        if (Entity_->List && Entity_->List->ObjectName)
            snprintf(out, outSize, "%s", Entity_->List->ObjectName);
        else
            snprintf(out, outSize, "Entity");

        return;
    }

    if (Kind == Selection::KIND_LAYER) {
        SceneLayer* layer = Selection::GetLayer();
        snprintf(out, outSize, "%s", layer && layer->Name[0] ? layer->Name : "Layer");
        return;
    }

    snprintf(out, outSize, "%s", "");
}
