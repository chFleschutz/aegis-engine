module;

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.core;
import aegis.math;
import aegis.reflect;
import aegis.render_passes;
import aegis.rhi;
import aegis.world;
import aegis.world.render_components;
import aegis.world.systems.transform_system;
import :panels;

namespace aegis::editor
{
namespace
{
// Light and camera icons: their radius in pixels, which is also how near a click has to be.
constexpr float iconRadius{ 9.0f };
// What a focus frames around an entity without a mesh.
constexpr float pointFocusRadius{ 1.0f };
// A scene dropped into the view lands on the ground under the cursor if it is this near, else this far along the ray.
constexpr float dropMaxDistance{ 100.0f };
constexpr float dropFallbackDistance{ 5.0f };

// The local bounds of the entity's mesh in world space; none without a loaded mesh.
auto meshBounds(world::Registry& registry, world::Entity entity) -> std::optional<Sphere>
{
    if (!registry.has<MeshRenderer, GlobalTransform>(entity))
        return std::nullopt;
    const auto* mesh = registry.get<MeshRenderer>(entity).mesh.get();
    if (!mesh)
        return std::nullopt;
    const auto& bounds = mesh->bounds();
    return transformSphere(Sphere{ .center = bounds.center, .radius = bounds.radius }, registry.get<GlobalTransform>(entity).matrix());
}

// The cursor in the view's ndc (y up).
auto toNdc(const ImVec2& cursor, float x, float y, float width, float height) -> glm::vec2
{
    return { (cursor.x - x) / width * 2.0f - 1.0f, 1.0f - (cursor.y - y) / height * 2.0f };
}
}

auto Viewport::aspect() const -> float
{
    const auto extent = m_extent.current();
    return extent ? static_cast<float>(extent->width) / static_cast<float>(extent->height) : 1.0f;
}

auto Viewport::renderView() const -> std::optional<RenderView>
{
    const auto extent = m_extent.current();
    if (!m_open || !extent)
        return std::nullopt;
    if (m_gameView)
        return RenderView{ .camera = std::nullopt, .extent = rhi::Extent2D{ extent->width, extent->height } };
    return RenderView{
        .camera = RenderView::Camera{ .view = m_camera.view(), .projection = m_camera.projection(aspect()), .position = m_camera.position() },
        .extent = rhi::Extent2D{ extent->width, extent->height },
    };
}

void Viewport::draw(EditorContext& editor, FrameContext& frame)
{
    const bool focusRequested = editor.takeFocusRequest(); // Taken even while the viewport can't show it
    if (!m_cameraPlaced)
        placeCamera(frame.world);

    bool visible = false;
    if (m_open)
    {
        ImGui::SetNextWindowSize(ImVec2{ 960.0f, 540.0f }, ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        visible = ImGui::Begin("Viewport", &m_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
    }
    if (visible)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const Rect rect{ .x = origin.x, .y = origin.y, .width = std::max(size.x, 1.0f), .height = std::max(size.y, 1.0f) };
        m_extent.update({ static_cast<std::uint32_t>(rect.width), static_cast<std::uint32_t>(rect.height) });

        // Not an item: ImGuizmo only starts a drag while no item is hovered (windows move by their title bar only)
        const ImVec2 cursor = ImGui::GetIO().MousePos;
        m_hovered = ImGui::IsWindowHovered() && cursor.x >= rect.x && cursor.y >= rect.y && cursor.x < rect.x + rect.width && cursor.y < rect.y + rect.height;
        m_focused = ImGui::IsWindowFocused();

        // The graph renders into the view the viewport asked for last frame; until then "Final" is the window's.
        // While a new size settles, the image stretches to the panel.
        const auto final = frame.frameGraph.declarations().find(render_graph::names::finalImage);
        if (frame.viewExtent && final)
        {
            if (const auto texture = frame.presentation.uiTexture(frame.frameGraph.pool().texture(*final).view))
                ImGui::GetWindowDrawList()->AddImage(ImTextureRef{ texture }, origin, ImVec2{ rect.x + rect.width, rect.y + rect.height });
        }
        if (editor.playing())
        {
            // The play world is not the edit world: a frame inside the image says so
            const float thickness = ImGui::GetFontSize() * 0.25f;
            const float inset = thickness * 0.5f;
            ImGui::GetWindowDrawList()->AddRect(ImVec2{ rect.x + inset, rect.y + inset }, ImVec2{ rect.x + rect.width - inset, rect.y + rect.height - inset }, theme::packed(theme::playFrame), 0.0f, 0, thickness);
        }

        if (!m_gameView)
        {
            updateCamera(editor, frame.deltaSeconds);
            if (focusRequested)
                focusSelection(editor);
            updateGizmoMode();
            const auto icons = drawIcons(editor, rect);
            const bool gizmo = drawGizmo(editor, frame.types, rect);
            drawToolbar(editor, rect);
            pick(editor, rect, icons, gizmo);
            acceptDrop(editor, frame, rect);
        }
    }
    else
    {
        m_hovered = false;
        m_focused = false;
        m_flying = m_orbiting = m_panning = false;
    }
    if (m_open)
        ImGui::End();
    if (!m_open)
        m_extent.reset();
}

void Viewport::setGameView(bool gameView)
{
    m_gameView = gameView;
    m_flying = m_orbiting = m_panning = false;
    m_gizmoDragging = false; // The shell seals the drag's undo entry
}

void Viewport::placeCamera(world::World& world)
{
    m_cameraPlaced = true;
    auto& registry = world.registry();
    const auto camera = world.mainCamera();
    if (!registry.has<Transform, Parent>(camera))
        return; // Null, or not placed: the default view
    // Before the first update the global transform may not be derived yet; a root camera's local one is its global
    const auto parent = registry.get<Parent>(camera).entity;
    const bool root = !parent || !registry.has<GlobalTransform>(camera);
    const auto location = root ? registry.get<Transform>(camera).location : registry.get<GlobalTransform>(camera).location;
    const auto forward = root ? registry.get<Transform>(camera).forward() : registry.get<GlobalTransform>(camera).forward();
    m_camera.lookAlong(location, forward);
}

void Viewport::updateCamera(EditorContext& editor, float deltaSeconds)
{
    const ImGuiIO& io = ImGui::GetIO();
    // A drag starts in the viewport and lasts while its button is held, even when the cursor leaves the panel
    if (m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        m_flying = true;
    if (m_hovered && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_orbiting = true;
    if (m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        m_panning = true;
    m_flying = m_flying && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    m_orbiting = m_orbiting && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    m_panning = m_panning && ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    CameraInput input{
        .cursorDelta = { io.MouseDelta.x, io.MouseDelta.y },
        .wheel = m_hovered || m_flying ? io.MouseWheel : 0.0f,
        .fly = m_flying,
        .orbit = m_orbiting,
        .pan = m_panning,
    };
    if (m_flying)
    {
        const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
            return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
        };
        input.move = { axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_W, ImGuiKey_S), axis(ImGuiKey_E, ImGuiKey_Q) };
    }

    auto& preferences = editor.preferences();
    m_camera.setSpeed(preferences.cameraSpeed);
    m_camera.update(input, deltaSeconds);
    preferences.cameraSpeed = std::clamp(m_camera.speed(), 0.01f, 1000.0f);
}

void Viewport::focusSelection(const EditorContext& editor)
{
    auto* world = editor.world();
    const auto selected = editor.selectedEntity();
    if (!world || !selected)
        return;
    auto& registry = world->registry();
    if (const auto bounds = meshBounds(registry, selected))
        m_camera.focus(bounds->center, bounds->radius);
    else if (registry.has<GlobalTransform>(selected))
        m_camera.focus(registry.get<GlobalTransform>(selected).location, pointFocusRadius);
}

void Viewport::updateGizmoMode()
{
    // Only with the viewport focused, and not while flying (WASD/QE move the camera) or typing
    const ImGuiIO& io = ImGui::GetIO();
    if (!m_focused || m_flying || io.WantTextInput || io.KeyCtrl || ImGuizmo::IsUsing())
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
        m_gizmoMode = GizmoMode::None;
    else if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        m_gizmoMode = GizmoMode::Translate;
    else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        m_gizmoMode = GizmoMode::Rotate;
    else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        m_gizmoMode = GizmoMode::Scale;
}

auto Viewport::drawGizmo(EditorContext& editor, const reflect::TypeRegistry& types, const Rect& rect) -> bool
{
    auto* world = editor.world();
    const auto selected = editor.selectedEntity();
    if (m_gizmoMode == GizmoMode::None || !world)
        return false;
    auto& registry = world->registry();
    if (!registry.valid(selected) || !registry.has<Transform, GlobalTransform, Parent>(selected))
        return false;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(rect.x, rect.y, rect.width, rect.height);
    ImGuizmo::Enable(!cameraDragged());

    const glm::mat4 view = m_camera.view();
    glm::mat4 projection = m_camera.projection(aspect());
    projection[1][1] *= -1.0f; // ImGuizmo expects y up

    // The gizmo works in world space; the result is converted back to the local transform
    auto& transform = registry.get<Transform>(selected);
    glm::mat4 matrix = registry.get<GlobalTransform>(selected).matrix();

    auto operation = ImGuizmo::OPERATION::TRANSLATE;
    const auto& preferences = editor.preferences();
    float snapValue = preferences.snapTranslate;
    if (m_gizmoMode == GizmoMode::Rotate)
    {
        operation = ImGuizmo::OPERATION::ROTATE;
        snapValue = preferences.snapRotate;
    }
    else if (m_gizmoMode == GizmoMode::Scale)
    {
        operation = ImGuizmo::OPERATION::SCALE;
        snapValue = preferences.snapScale;
    }
    const std::array snapValues{ snapValue, snapValue, snapValue };
    const bool snapping = m_snap != ImGui::GetIO().KeyCtrl;
    // Scaling is always along the entity's own axes
    const auto mode = m_gizmoSpace == GizmoSpace::World && m_gizmoMode != GizmoMode::Scale ? ImGuizmo::MODE::WORLD : ImGuizmo::MODE::LOCAL;

    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode, glm::value_ptr(matrix), nullptr,
        snapping ? snapValues.data() : nullptr);
    // One drag is one undo entry: it stays open while the gizmo is in use
    const bool dragging = ImGuizmo::IsUsing();
    if (m_gizmoDragging && !dragging)
        editor.undoStack().seal();
    m_gizmoDragging = dragging;
    if (!changed)
        return true;

    const auto* transformType = types.find("Transform");
    const auto oldValues = transformType ? fieldValues(*transformType, &transform) : FieldValues{};
    GlobalTransform parentGlobal{};
    const auto parent = registry.get<Parent>(selected).entity;
    if (parent && registry.has<GlobalTransform>(parent))
        parentGlobal = registry.get<GlobalTransform>(parent);
    transform = world::detail::toLocal(parentGlobal, matrix);
    if (!registry.has<DynamicTag>(selected))
        registry.markStaticDirty(selected); // Static entities only move when told to
    if (transformType)
    {
        editor.undoStack().record(ComponentEdit{ .entity = registry.get<StableId>(selected), .type = transformType->name, .oldValues = oldValues, .newValues = fieldValues(*transformType, &transform) },
            dragging);
    }
    return true;
}

void Viewport::drawToolbar(EditorContext& editor, const Rect& rect)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorScreenPos(ImVec2{ rect.x + style.ItemSpacing.x, rect.y + style.ItemSpacing.y });

    const auto modeButton = [this](const char* icon, const char* id, GizmoMode mode, const char* tooltip) {
        if (theme::iconButton(icon, id, m_gizmoMode == mode))
            m_gizmoMode = mode;
        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine();
    };
    modeButton(theme::icons::select, "select", GizmoMode::None, "Select (Q)");
    modeButton(theme::icons::move, "move", GizmoMode::Translate, "Move (W)");
    modeButton(theme::icons::rotate, "rotate", GizmoMode::Rotate, "Rotate (E)");
    modeButton(theme::icons::scale, "scale", GizmoMode::Scale, "Scale (R)");

    ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
    const bool local = m_gizmoSpace == GizmoSpace::Local;
    if (theme::iconButton(local ? theme::icons::local : theme::icons::world, "space"))
        m_gizmoSpace = local ? GizmoSpace::World : GizmoSpace::Local;
    ImGui::SetItemTooltip("%s axes: the entity's own or the world's, click to switch (scaling always uses the entity's)", local ? "Local" : "World");
    ImGui::SameLine();
    if (theme::iconButton(theme::icons::snap, "snap", m_snap))
        m_snap = !m_snap;
    ImGui::SetItemTooltip("Snap (Ctrl inverts it). Steps: %.3g units, %.3g degrees, %.3g scale", editor.preferences().snapTranslate,
        editor.preferences().snapRotate, editor.preferences().snapScale);
}

auto Viewport::drawIcons(const EditorContext& editor, const Rect& rect) -> std::vector<PickIcon>
{
    std::vector<PickIcon> icons;
    auto* world = editor.world();
    if (!world)
        return icons;
    auto& registry = world->registry();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(ImVec2{ rect.x, rect.y }, ImVec2{ rect.x + rect.width, rect.y + rect.height }, true);

    // The Lucide glyph in the merged font; the letter without it
    const bool glyphs = theme::fonts().ui != nullptr;
    const auto drawIcon = [&](entt::entity handle, const char* glyph, const char* letter, const ImVec4& tint) {
        const ImU32 color = theme::packed(tint);
        const char* label = glyphs ? glyph : letter;
        const world::Entity entity{ handle };
        if (!registry.has<GlobalTransform, StableId>(entity))
            return;
        const auto ndc = m_camera.project(registry.get<GlobalTransform>(entity).location, aspect());
        if (!ndc || std::abs(ndc->x) > 1.0f || std::abs(ndc->y) > 1.0f)
            return;
        const ImVec2 center{ rect.x + (ndc->x + 1.0f) * 0.5f * rect.width, rect.y + (1.0f - ndc->y) * 0.5f * rect.height };
        const bool selected = editor.isSelected(entity);
        drawList->AddCircleFilled(center, iconRadius, theme::packed(theme::iconBackdrop));
        drawList->AddCircle(center, iconRadius, selected ? theme::packed(theme::iconSelected) : color, 0, selected ? 2.5f : 1.5f);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        drawList->AddText(ImVec2{ center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f }, color, label);
        icons.push_back({ .id = registry.get<StableId>(entity), .position = { center.x, center.y } });
    };
    for (const auto entity : registry.view<PointLight>())
        drawIcon(entity, theme::icons::light, "P", theme::lightIcon);
    for (const auto entity : registry.view<DirectionalLight>())
        drawIcon(entity, theme::icons::sun, "D", theme::lightIcon);
    for (const auto entity : registry.view<Camera>())
        drawIcon(entity, theme::icons::camera, "C", theme::cameraIcon);

    drawList->PopClipRect();
    return icons;
}

void Viewport::acceptDrop(EditorContext& editor, FrameContext& frame, const Rect& rect)
{
    // The image is no item: its rectangle takes the drop
    const ImRect bounds{ rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
    if (!ImGui::BeginDragDropTargetCustom(bounds, ImGui::GetID("##drop")))
        return;
    if (const auto dropped = acceptAssetPayload([](const AssetPayload& payload) { return canInstantiate(payload); }))
    {
        const auto ray = m_camera.ray(toNdc(ImGui::GetIO().MousePos, rect.x, rect.y, rect.width, rect.height), aspect());
        instantiateAsset(editor, frame, dropped->id, {}, dropPoint(ray, dropMaxDistance, dropFallbackDistance));
    }
    ImGui::EndDragDropTarget();
}

void Viewport::pick(EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons, bool gizmo)
{
    // A plain left click on the image: not orbiting, not on this frame's gizmo or the toolbar
    const ImGuiIO& io = ImGui::GetIO();
    if (!m_hovered || io.KeyAlt || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || (gizmo && ImGuizmo::IsOver()) || ImGui::IsAnyItemHovered())
        return;
    const ImVec2 cursor = io.MousePos;
    auto* world = editor.world();
    if (!world)
        return;

    auto picked = pickIcon({ cursor.x, cursor.y }, icons, iconRadius);
    if (!picked)
    {
        auto& registry = world->registry();
        std::vector<PickTarget> targets;
        for (const auto entity : registry.view<MeshRenderer, GlobalTransform, StableId>())
        {
            if (const auto bounds = meshBounds(registry, world::Entity{ entity }))
                targets.push_back({ .id = registry.get<StableId>(world::Entity{ entity }), .bounds = *bounds });
        }
        picked = pickNearest(m_camera.ray(toNdc(cursor, rect.x, rect.y, rect.width, rect.height), aspect()), targets);
    }

    if (picked)
        editor.selection().select(*picked);
    else
        editor.selection().clear();
}
}
