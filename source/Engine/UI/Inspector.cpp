#if INTERFACE
#include <Engine/Includes/Standard.h>

need_t Entity;
need_t SceneLayer;

class Inspector {
public:

};
#endif

#include <Engine/UI/Inspector.h>

#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Types/Entity.h>
#include <Engine/Types/ObjectList.h>
#include <Engine/UI/Selection.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UITheme.h>

// The properties of whatever is selected, and a way to change them.
//
// Unity's inspector draws whatever object the selection points at, and edits go
// straight into the live object so the change is visible in the scene view the
// same frame. That immediacy is the whole point of the thing -- an inspector
// you have to press Apply on is a form, not an inspector -- and it is what this
// does: the fields below are the running entity's own members.
//
// Unity builds its inspectors out of SerializedObject and SerializedProperty so
// a type can be drawn without the editor knowing anything about it. That needs
// reflection, which C++ does not have and this engine does not fake, so the
// fields here are named. It is the honest version of the same idea: the
// alternative is a reflection system nobody asked for.

PRIVATE STATIC void Inspector::DrawEntity(Entity* entity) {
    const char* name = (entity->List && entity->List->ObjectName)
        ? entity->List->ObjectName : "Entity";

    UICore::Field("Object", name);

    UICore::Separator();
    UICore::Heading("Transform");

    // These write straight into the entity. The scene view is drawing the same
    // memory, so dragging a number here moves the thing on screen as it is
    // typed rather than when the field is left.
    UICore::FloatField("X", &entity->X);
    UICore::FloatField("Y", &entity->Y);
    UICore::FloatField("Z", &entity->Z);

    UICore::FloatField("Scale X", &entity->ScaleX);
    UICore::FloatField("Scale Y", &entity->ScaleY);
    UICore::FloatField("Rotation", &entity->Rotation);
    UICore::IntField("Angle", &entity->Angle);

    UICore::Separator();
    UICore::Heading("Motion");

    UICore::FloatField("X speed", &entity->XSpeed);
    UICore::FloatField("Y speed", &entity->YSpeed);
    UICore::FloatField("Ground speed", &entity->GroundSpeed);
    UICore::FloatField("Gravity", &entity->Gravity);

    UICore::Separator();
    UICore::Heading("Appearance");

    UICore::FloatField("Alpha", &entity->Alpha);
    UICore::IntField("Priority", &entity->Priority);

    UICore::Separator();
    UICore::Heading("State");

    // Active and the rest are ints used as booleans, which the checkbox has to
    // be told about rather than handed a bool it cannot point at.
    bool active = entity->Active != 0;
    if (UICore::Checkbox("Active", &active))
        entity->Active = active ? 1 : 0;

    bool pauseable = entity->Pauseable != 0;
    if (UICore::Checkbox("Pauseable", &pauseable))
        entity->Pauseable = pauseable ? 1 : 0;

    bool interactable = entity->Interactable != 0;
    if (UICore::Checkbox("Interactable", &interactable))
        entity->Interactable = interactable ? 1 : 0;

    UICore::Field("On screen", entity->OnScreen ? "yes" : "no");
    UICore::FieldFormatted("Start", "%d, %d", (int)entity->InitialX, (int)entity->InitialY);
}

PRIVATE STATIC void Inspector::DrawLayer(SceneLayer* layer, int index) {
    UICore::Field("Layer", layer->Name[0] ? layer->Name : "Unnamed");
    UICore::FieldFormatted("Index", "%d", index);

    UICore::Separator();
    UICore::Heading("Size");

    UICore::FieldFormatted("In tiles", "%d x %d", layer->Width, layer->Height);
    UICore::FieldFormatted("In pixels", "%d x %d",
        layer->Width * Scene::TileWidth, layer->Height * Scene::TileHeight);

    // Worth showing rather than hiding: a layer's rows are stored this far
    // apart, not Width apart, and anything walking the tile array by hand has
    // to know it.
    UICore::FieldFormatted("Row stride", "%d", (int)layer->WidthData);

    UICore::Separator();
    UICore::Heading("Drawing");

    UICore::Checkbox("Visible", &layer->Visible);
    UICore::IntField("Draw group", &layer->DrawGroup);
    UICore::FloatField("Opacity", &layer->Opacity);
    UICore::Checkbox("Blending", &layer->Blending);

    UICore::Separator();
    UICore::Heading("Scroll");

    UICore::IntField("Offset X", &layer->OffsetX);
    UICore::IntField("Offset Y", &layer->OffsetY);
    UICore::IntField("Relative Y", &layer->RelativeY);
    UICore::IntField("Constant Y", &layer->ConstantY);
}

PUBLIC STATIC void Inspector::Draw() {
    switch (Selection::GetKind()) {
        case Selection::KIND_ENTITY: {
            Entity* entity = Selection::GetEntity();
            if (entity)
                Inspector::DrawEntity(entity);
            break;
        }

        case Selection::KIND_LAYER: {
            SceneLayer* layer = Selection::GetLayer();
            if (layer)
                Inspector::DrawLayer(layer, Selection::GetLayerIndex());
            break;
        }

        default:
            UICore::Text("Nothing is selected.", UI_COL_TEXT_FAINT);
            UICore::Text("Pick something from the hierarchy, or click", UI_COL_TEXT_FAINT);
            UICore::Text("an entity in the scene editor.", UI_COL_TEXT_FAINT);
            break;
    }
}
