module;

#include <cfloat>

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
import :theme;

namespace aegis::editor
{
namespace
{
// Light and camera icons: their radius in pixels at 100%, which scaled is also how near a click has to be. Icons
// farther from the camera than iconFadeStart shrink and fade linearly until iconFadeEnd, from where on they keep
// these shares of their radius and opacity.
constexpr float iconRadius{ 9.0f };
constexpr float iconFadeStart{ 10.0f };
constexpr float iconFadeEnd{ 40.0f };
constexpr float iconMinScale{ 0.4f };
constexpr float iconMinAlpha{ 0.35f };
// The camera speed's range (EditorPreferences::clamp keeps it too), and how far a pixel of drag moves it across
// that range, which it covers logarithmically.
constexpr float minCameraSpeed{ 0.01f };
constexpr float maxCameraSpeed{ 1000.0f };
constexpr float speedDragRate{ 0.002f };
// The overlays in frame heights: the speed pill's width, the navigation gizmo's radius to the axis ends and each
// end's disc. The negative ends' discs are dimmer: their fill keeps this share of the axis color's opacity.
constexpr float speedPillWidth{ 5.0f };
constexpr float navigationRadius{ 1.8f };
constexpr float navigationDisc{ 0.42f };
constexpr float navigationNegativeAlpha{ 0.4f };
// What a focus frames around an entity without a mesh.
constexpr float pointFocusRadius{ 1.0f };
// A scene dropped into the view lands on the ground under the cursor if it is this near, else this far along the ray.
constexpr float dropMaxDistance{ 100.0f };
constexpr float dropFallbackDistance{ 5.0f };
// The brackets around the selected and hovered entities, in pixels at 100%: a box smaller than bracketMinSize grows to
// it, each arm is a quarter of its side but at most bracketMaxArm, and an icon's box is bracketIconGap outside its
// ring. The active entity's are the accent at full strength and wider; the other selected and the hovered entity's
// are fainter. Each sits on a dark outline so it reads on any scene.
constexpr float bracketMinSize{ 12.0f };
constexpr float bracketMaxArm{ 16.0f };
constexpr float bracketIconGap{ 3.0f };
constexpr float bracketActiveWidth{ 2.0f };
constexpr float bracketWidth{ 1.5f };
constexpr float bracketSelectedAlpha{ 0.7f };
constexpr float bracketHoverAlpha{ 0.45f };
constexpr float bracketOutlineAlpha{ 0.6f };

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

// Corner brackets around 'box' in the accent at 'alpha', over a dark outline, 'width' pixels wide at 100%.
void drawCornerBrackets(ImDrawList* drawList, ImRect box, float alpha, float width)
{
    const float scale = theme::scale();
    const ImVec2 center = box.GetCenter();
    const float halfMin = bracketMinSize * scale * 0.5f;
    box.Min = ImVec2{ std::min(box.Min.x, center.x - halfMin), std::min(box.Min.y, center.y - halfMin) };
    box.Max = ImVec2{ std::max(box.Max.x, center.x + halfMin), std::max(box.Max.y, center.y + halfMin) };
    const float armX = std::min(box.GetWidth() * 0.25f, bracketMaxArm * scale);
    const float armY = std::min(box.GetHeight() * 0.25f, bracketMaxArm * scale);

    ImVec4 color = theme::accent;
    color.w *= alpha;
    ImVec4 outline = theme::bgDeep;
    outline.w = bracketOutlineAlpha * alpha;
    // Each corner, and the directions its arms point in: into the box
    const std::array corners{
        std::pair{ box.Min, ImVec2{ 1.0f, 1.0f } },
        std::pair{ ImVec2{ box.Max.x, box.Min.y }, ImVec2{ -1.0f, 1.0f } },
        std::pair{ box.Max, ImVec2{ -1.0f, -1.0f } },
        std::pair{ ImVec2{ box.Min.x, box.Max.y }, ImVec2{ 1.0f, -1.0f } },
    };
    for (const bool shadow : { true, false })
    {
        for (const auto& [corner, inward] : corners)
        {
            const std::array points{ ImVec2{ corner.x, corner.y + inward.y * armY }, corner, ImVec2{ corner.x + inward.x * armX, corner.y } };
            drawList->AddPolyline(points.data(), static_cast<int>(points.size()), theme::packed(shadow ? outline : color), ImDrawFlags_None,
                (shadow ? width + 2.0f : width) * scale);
        }
    }
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

auto Viewport::snapping() const -> bool
{
    return m_snap != ImGui::GetIO().KeyCtrl;
}

auto Viewport::renderView() const -> std::optional<RenderView>
{
    const auto extent = m_extent.current();
    if (!extent)
        return std::nullopt;
    if (m_gameView)
        return RenderView{ .camera = std::nullopt, .extent = rhi::Extent2D{ extent->width, extent->height } };
    return RenderView{
        .camera = RenderView::Camera{ .view = m_camera.view(), .projection = m_camera.projection(aspect()), .position = m_camera.position() },
        .extent = rhi::Extent2D{ extent->width, extent->height },
    };
}

void Viewport::draw(EditorContext& editor, FrameContext& frame, const std::function<void()>& bar, const std::function<void()>& topBar)
{
    const bool focusRequested = editor.takeFocusRequest(); // Taken even while the viewport can't show it
    if (focusRequested)
        m_hoverCurrent = false; // The camera moves under the cursor
    if (!m_cameraPlaced)
        placeCamera(frame.world);

    // Alone in its dock node the viewport has no tab bar; it can't be closed, only undocked
    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowSize(ImVec2{ 960.0f * theme::scale(), 540.0f * theme::scale() }, ImGuiCond_FirstUseEver);
    if (!m_fullWindow && std::exchange(m_moveFocus, false))
        ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    bool visible = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (m_fullWindow)
    {
        // Submitted but empty, so its dock node and the layout stay; the image goes into the window over the display
        ImGui::End();
        visible = beginFullWindow();
    }
    bool gizmo = false;
    if (visible)
    {
        if (bar)
            drawBar(bar);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const Rect rect{ .x = origin.x, .y = origin.y, .width = std::max(size.x, 1.0f), .height = std::max(size.y, 1.0f) };
        m_imageRect = rect;
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
        if (givesInputToGame() && !m_releaseHint.empty())
            drawCornerLabel(rect, m_releaseHint);
        if (topBar)
            drawTopBar(rect, topBar);

        if (!m_gameView)
        {
            updateCamera(editor, frame.deltaSeconds);
            if (focusRequested)
                focusSelection(editor);
            updateGizmoMode();
            const auto icons = drawIcons(editor, rect);
            drawBrackets(editor, rect, icons);
            gizmo = drawGizmo(editor, frame, rect);
            drawToolbar(editor, rect);
            drawOverlayCluster(editor, rect);
            if (editor.preferences().showStats)
                drawStatsOverlay(editor, frame, rect);
            updateHover(editor, rect, icons, gizmo);
            pick(editor, rect, icons, gizmo);
            acceptDrop(editor, frame, rect);
        }
    }
    else
    {
        m_imageRect.reset();
        m_hovered = false;
        m_focused = false;
        m_flying = m_orbiting = m_panning = false;
        m_hoveredId.reset();
        m_hoverCurrent = false;
    }
    // The drag ends when the gizmo is released, or no longer drawn (the selection changed under it)
    if (!gizmo || !ImGuizmo::IsUsing())
        endGizmoDrag(editor, frame);
    ImGui::End();
}

void Viewport::setFullWindow(bool fullWindow)
{
    if (fullWindow == m_fullWindow)
        return;
    m_fullWindow = fullWindow;
    m_moveFocus = m_focused;
    m_flying = m_orbiting = m_panning = false; // The image moves under the cursor
    m_hoverCurrent = false;
}

auto Viewport::beginFullWindow() -> bool
{
    const ImGuiViewport* display = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(display->Pos);
    ImGui::SetNextWindowSize(display->Size);
    ImGui::SetNextWindowViewport(display->ID);
    if (std::exchange(m_moveFocus, false))
        ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // Over the docked panels, which are still drawn below it. It comes to the front as it appears, not on focus,
    // so the drawer, which takes the focus when it opens, shows over it.
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    const bool visible = ImGui::Begin("##FullViewport", nullptr, flags);
    ImGui::PopStyleVar(3);
    if (ImGui::IsWindowAppearing())
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    return visible;
}

void Viewport::drawTopBar(const Rect& rect, const std::function<void()>& topBar)
{
    // A child window: the cursor over it is not over the image, so it neither picks nor moves the camera
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2{ std::round(rect.x + (rect.width - m_topBarWidth) * 0.5f), rect.y + style.ItemSpacing.y * 2.0f });
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bgPopup);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.FrameRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ style.ItemSpacing.x, style.FramePadding.y });
    const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY |
                                       ImGuiChildFlags_AlwaysAutoResize;
    if (ImGui::BeginChild("##topBar", ImVec2{ 0.0f, 0.0f }, childFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        topBar();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    m_topBarWidth = ImGui::GetItemRectSize().x;
    ImGui::SetCursorScreenPos(cursor);
}

void Viewport::releaseInput()
{
    ImGui::SetWindowFocus(nullptr);
    m_focused = false;
}

void Viewport::setGameView(bool gameView)
{
    m_gameView = gameView;
    m_flying = m_orbiting = m_panning = false;
    m_hoveredId.reset();
    m_hoverCurrent = false;
    m_gizmoDrag.reset(); // A drag still going belongs to the world left behind: the shell records it first (endGizmoDrag)
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
    // A drag starts in the viewport, not on an overlay (the tool buttons, the gizmo, ...), and lasts while its button is
    // held, even when the cursor leaves the panel. The overlays come later in the frame: this asks about the last one's.
    const bool canStart = m_hovered && !ImGui::IsAnyItemHovered();
    if (canStart && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        m_flying = true;
    if (canStart && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_orbiting = true;
    if (canStart && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
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
    const float speed = preferences.cameraSpeed;
    m_camera.setSpeed(speed);
    m_camera.update(input, deltaSeconds);
    // The wheel changes the speed while flying; the speed pill shows it
    preferences.cameraSpeed = std::clamp(m_camera.speed(), minCameraSpeed, maxCameraSpeed);
}

void Viewport::focusSelection(const EditorContext& editor)
{
    auto* world = editor.world();
    if (!world)
        return;
    // The union of the selected roots' bounds: their meshes, or a point's sphere for one without
    auto& registry = world->registry();
    std::optional<Sphere> framed;
    for (const auto root : selectedRoots(registry, editor.selection()))
    {
        auto bounds = meshBounds(registry, root);
        if (!bounds && registry.has<GlobalTransform>(root))
            bounds = Sphere{ .center = registry.get<GlobalTransform>(root).location, .radius = pointFocusRadius };
        if (bounds)
            framed = framed ? enclose(*framed, *bounds) : *bounds;
    }
    if (framed)
        m_camera.focus(framed->center, framed->radius);
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

auto Viewport::drawGizmo(EditorContext& editor, FrameContext& frame, const Rect& rect) -> bool
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

    // The gizmo works in world space; the result is converted back to the local transforms
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
    // Scaling is always along the entity's own axes
    const auto mode = m_gizmoSpace == GizmoSpace::World && m_gizmoMode != GizmoMode::Scale ? ImGuizmo::MODE::WORLD : ImGuizmo::MODE::LOCAL;

    const glm::mat4 before = matrix;
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode, glm::value_ptr(matrix), nullptr,
        snapping() ? snapValues.data() : nullptr);
    const auto* transformType = frame.types.find<Transform>();
    if (!transformType)
        return true;
    if ((changed || ImGuizmo::IsUsing()) && !m_gizmoDrag)
        beginGizmoDrag(editor, *transformType, before);
    if (!changed || !m_gizmoDrag)
        return true;

    // The change of the active entity's world matrix since the drag began, applied to every root's
    const glm::mat4 delta = matrix * glm::inverse(m_gizmoDrag->start);
    const auto active = registry.get<StableId>(selected);
    const auto prepare = editWorld(editor, frame).prepare;
    for (const auto& root : m_gizmoDrag->roots)
    {
        const auto entity = registry.find(root.id);
        if (!entity || !registry.has<Transform, Parent>(entity))
            continue;
        GlobalTransform parentGlobal{};
        const auto parent = registry.get<Parent>(entity).entity;
        if (parent && registry.has<GlobalTransform>(parent))
            parentGlobal = registry.get<GlobalTransform>(parent);
        const Transform moved = world::detail::toLocal(parentGlobal, root.id == active ? matrix : delta * root.start);
        // Through the change hook: a static entity only moves when told to
        auto& transform = registry.get<Transform>(entity);
        if (auto written = writeComponent(*transformType, registry, entity, &transform, fieldValues(*transformType, &moved), prepare); !written)
            log::warn("Cannot move the entity: {}", reflect::describe(written.error()));
    }
    return true;
}

void Viewport::beginGizmoDrag(EditorContext& editor, const reflect::TypeInfo& transformType, const glm::mat4& start)
{
    auto& registry = editor.world()->registry();
    GizmoDrag drag{ .start = start };
    drag.verb = m_gizmoMode == GizmoMode::Rotate ? "Rotate" : (m_gizmoMode == GizmoMode::Scale ? "Scale" : "Move");
    for (const auto root : selectedRoots(registry, editor.selection()))
    {
        if (!registry.has<Transform, GlobalTransform>(root))
            continue;
        drag.roots.push_back({
            .id = registry.get<StableId>(root),
            .start = registry.get<GlobalTransform>(root).matrix(),
            .oldValues = fieldValues(transformType, &registry.get<Transform>(root)),
        });
    }
    m_gizmoDrag = std::move(drag);
}

void Viewport::endGizmoDrag(EditorContext& editor, FrameContext& frame)
{
    if (!m_gizmoDrag)
        return;
    auto drag = std::move(*m_gizmoDrag);
    m_gizmoDrag.reset();
    auto* world = editor.world();
    const auto* transformType = frame.types.find<Transform>();
    if (!world || !transformType)
        return;

    // One undo entry for the drag: each root's Transform from before it to now
    auto& registry = world->registry();
    auto& history = editor.undoStack();
    std::optional<UndoGroup> group;
    if (drag.roots.size() > 1)
        group.emplace(history, std::format("{} {} entities", drag.verb, drag.roots.size()));
    for (auto& root : drag.roots)
    {
        const auto entity = registry.find(root.id);
        if (!entity || !registry.has<Transform>(entity))
            continue;
        history.record(ComponentEdit{
            .entity = root.id,
            .type = transformType->name,
            .oldValues = std::move(root.oldValues),
            .newValues = fieldValues(*transformType, &registry.get<Transform>(entity)),
        });
    }
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
    // Lit while snapping is in effect, so holding Ctrl flips it
    const bool snap = snapping();
    if (theme::iconButton(theme::icons::snap, "snap", snap))
        m_snap = !m_snap;
    ImGui::SetItemTooltip("Snap %s%s, click to turn it %s; holding Ctrl inverts it. Steps: %.3g units, %.3g degrees, %.3g scale", snap ? "on" : "off",
        ImGui::GetIO().KeyCtrl ? " (Ctrl held)" : "", snap ? "off" : "on", editor.preferences().snapTranslate, editor.preferences().snapRotate,
        editor.preferences().snapScale);
    ImGui::SameLine();
    bool& showIcons = editor.preferences().showIcons;
    if (theme::iconButton(showIcons ? theme::icons::shown : theme::icons::hidden, "icons"))
        showIcons = !showIcons;
    ImGui::SetItemTooltip("Light and camera icons: %s, click to %s them", showIcons ? "shown" : "hidden", showIcons ? "hide" : "show");
}

void Viewport::drawOverlayCluster(EditorContext& editor, const Rect& rect)
{
    // Items, as the tool buttons are: a click on one neither picks nor starts a camera drag, and the viewport keeps the focus
    const ImGuiStyle& style = ImGui::GetStyle();
    const float frameHeight = ImGui::GetFrameHeight();
    const float pillWidth = speedPillWidth * frameHeight;
    const float right = rect.x + rect.width - style.ItemSpacing.x;
    const float top = rect.y + style.ItemSpacing.y;
    ImGui::SetCursorScreenPos(ImVec2{ right - frameHeight - style.ItemSpacing.x - pillWidth, top });

    // The camera speed: a drag changes it, a click without moving types it
    auto& preferences = editor.preferences();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::iconBackdrop);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::bgFrameHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme::bgFrameActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, frameHeight * 0.5f);
    ImGuiIO& io = ImGui::GetIO();
    const bool clickToInput = std::exchange(io.ConfigDragClickToInputText, true); // For this widget only
    ImGui::SetNextItemWidth(pillWidth);
    ImGui::DragFloat("##cameraSpeed", &preferences.cameraSpeed, speedDragRate, minCameraSpeed, maxCameraSpeed,
        preferences.cameraSpeed < 1.0f ? "%.2f u/s" : "%.1f u/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    io.ConfigDragClickToInputText = clickToInput;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltip("Camera speed while flying (right mouse button + WASD/QE), in units per second: drag to change it, click to "
                          "type it. The wheel changes it while flying.");

    ImGui::SameLine();
    bool& showStats = preferences.showStats;
    if (theme::iconButton(theme::icons::stats, "stats", showStats))
        showStats = !showStats;
    ImGui::SetItemTooltip("Stats: %s, click to %s them", showStats ? "shown" : "hidden", showStats ? "hide" : "show");

    const float gizmoSize = 2.0f * (navigationRadius + navigationDisc) * frameHeight;
    drawNavigationGizmo({ right, top + frameHeight + style.ItemSpacing.y }, gizmoSize);
}

void Viewport::drawNavigationGizmo(const glm::vec2& corner, float size)
{
    const float frameHeight = ImGui::GetFrameHeight();
    const float radius = navigationRadius * frameHeight;
    const float disc = navigationDisc * frameHeight;
    const ImVec2 min{ corner.x - size, corner.y };
    const ImVec2 center{ min.x + size * 0.5f, min.y + size * 0.5f };
    // One item over the whole circle, so a click between the ends doesn't pick either
    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton("##navigation", ImVec2{ size, size });
    const bool hovered = ImGui::IsItemHovered();

    const auto axes = navigationAxes(m_camera);
    const ImVec2 cursor = ImGui::GetIO().MousePos;
    const auto underCursor = hovered ? pickNavigationAxis(axes, glm::vec2{ cursor.x - center.x, cursor.y - center.y } / radius, disc / radius) : std::nullopt;
    const auto isUnderCursor = [&](const NavigationAxis& axis) {
        return underCursor && underCursor->axis == axis.axis && underCursor->positive == axis.positive;
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 backdrop = theme::iconBackdrop;
    backdrop.w *= hovered ? 1.0f : 0.5f;
    drawList->AddCircleFilled(center, size * 0.5f, theme::packed(backdrop));
    const std::array colors{ theme::axisX, theme::axisY, theme::axisZ };
    const std::array labels{ "X", "Y", "Z" };
    const float fontSize = ImGui::GetFontSize() * 0.8f;
    const float scale = theme::scale();
    for (const auto& axis : axes) // The farthest first
    {
        const ImVec2 end{ center.x + axis.offset.x * radius, center.y + axis.offset.y * radius };
        const ImVec4 color = colors[static_cast<std::size_t>(axis.axis)];
        ImVec4 fill = color;
        if (axis.positive)
            drawList->AddLine(center, end, theme::packed(color), 2.0f * scale);
        else
            fill.w *= navigationNegativeAlpha;
        drawList->AddCircleFilled(end, disc, theme::packed(fill));
        const bool highlighted = isUnderCursor(axis);
        drawList->AddCircle(end, disc, theme::packed(highlighted ? theme::text : color), 0, (highlighted ? 2.0f : 1.0f) * scale);
        // The positive ends' letters are bgDeep, as on the inspector's strips; the dim negative ends' are the text color
        const char* label = labels[static_cast<std::size_t>(axis.axis)];
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
        drawList->AddText(ImGui::GetFont(), fontSize, ImVec2{ end.x - textSize.x * 0.5f, end.y - textSize.y * 0.5f },
            theme::packed(axis.positive ? theme::bgDeep : theme::text), label);
    }

    if (!underCursor)
        return;
    const char* label = labels[static_cast<std::size_t>(underCursor->axis)];
    ImGui::SetTooltip("View from %c%s, looking along %c%s", underCursor->positive ? '+' : '-', label, underCursor->positive ? '-' : '+', label);
    if (clicked)
    {
        // The clicked end then points at the viewer
        m_camera.orbitToward(-underCursor->direction);
        m_hoverCurrent = false; // The view moved under the cursor
    }
}

void Viewport::drawStatsOverlay(const EditorContext& editor, const FrameContext& frame, const Rect& rect) const
{
    // Below the tool buttons, on the icons' backdrop
    const ImGuiStyle& style = ImGui::GetStyle();
    const auto summary = statisticsSummary(editor, frame);
    const std::array rows{
        std::pair{ "Entities", std::format("{}", summary.entities) },
        std::pair{ "Draw batches", std::format("{}", summary.drawBatches) },
        std::pair{ "Instances", std::format("{}", summary.instances) },
    };
    float labelWidth = 0.0f;
    float valueWidth = 0.0f;
    for (const auto& [label, value] : rows)
    {
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(label).x);
        valueWidth = std::max(valueWidth, ImGui::CalcTextSize(value.c_str()).x);
    }
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 padding = style.FramePadding;
    const ImVec2 min{ rect.x + style.ItemSpacing.x, rect.y + style.ItemSpacing.y * 2.0f + ImGui::GetFrameHeight() };
    const ImVec2 size{ labelWidth + style.ItemSpacing.x * 2.0f + valueWidth + padding.x * 2.0f,
        lineHeight * static_cast<float>(rows.size()) - style.ItemSpacing.y + padding.y * 2.0f };

    // An item, so a click on the box doesn't pick through it
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##stats", size);
    ImGui::SetItemTooltip("Debug > Statistics has more");

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, ImVec2{ min.x + size.x, min.y + size.y }, theme::packed(theme::iconBackdrop), style.FrameRounding);
    float y = min.y + padding.y;
    for (const auto& [label, value] : rows)
    {
        drawList->AddText(ImVec2{ min.x + padding.x, y }, theme::packed(theme::textDim), label);
        const float width = ImGui::CalcTextSize(value.c_str()).x;
        drawList->AddText(ImVec2{ min.x + size.x - padding.x - width, y }, theme::packed(theme::text), value.c_str());
        y += lineHeight;
    }
}

void Viewport::drawBar(const std::function<void()>& bar)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 size{ ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + 2.0f * style.FramePadding.y };
    ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2{ start.x + size.x, start.y + size.y }, theme::packed(theme::bgPopup));
    ImGui::SetCursorScreenPos(ImVec2{ start.x + style.ItemSpacing.x, start.y + style.FramePadding.y });
    bar();
    // An item the size of the row moves the cursor below it, where the image starts
    ImGui::SetCursorScreenPos(start);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f });
    ImGui::Dummy(size);
    ImGui::PopStyleVar();
}

void Viewport::drawCornerLabel(const Rect& rect, const std::string& text) const
{
    // In the bottom left corner, inside the play frame, on the icons' backdrop so it reads over any scene
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 min{ rect.x + style.ItemSpacing.x * 2.0f, rect.y + rect.height - style.ItemSpacing.y * 2.0f - size.y - style.FramePadding.y * 2.0f };
    const ImVec2 max{ min.x + size.x + style.FramePadding.x * 2.0f, min.y + size.y + style.FramePadding.y * 2.0f };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, theme::packed(theme::iconBackdrop), style.FrameRounding);
    drawList->AddText(ImVec2{ min.x + style.FramePadding.x, min.y + style.FramePadding.y }, theme::packed(theme::textDim), text.c_str());
}

auto Viewport::drawIcons(const EditorContext& editor, const Rect& rect) -> std::vector<PickIcon>
{
    std::vector<PickIcon> icons;
    auto* world = editor.world();
    if (!world || !editor.preferences().showIcons)
        return icons; // Hidden icons can't be clicked either
    auto& registry = world->registry();

    // The Lucide glyph in the merged font; the letter without it
    const bool glyphs = theme::fonts().ui != nullptr;
    struct Icon
    {
        PickIcon pick;
        const char* label{ nullptr };
        ImVec4 tint;
        float scale{ 1.0f }; // Of the radius and the glyph
        bool selected{ false };
    };
    std::vector<Icon> shown;
    const auto addIcon = [&](entt::entity handle, const char* glyph, const char* letter, ImVec4 tint) {
        const world::Entity entity{ handle };
        if (!registry.has<GlobalTransform, StableId>(entity))
            return;
        const glm::vec3 location = registry.get<GlobalTransform>(entity).location;
        const auto ndc = m_camera.project(location, aspect());
        if (!ndc || std::abs(ndc->x) > 1.0f || std::abs(ndc->y) > 1.0f)
            return;
        const glm::vec2 center{ rect.x + (ndc->x + 1.0f) * 0.5f * rect.width, rect.y + (1.0f - ndc->y) * 0.5f * rect.height };
        const float fade = std::clamp((glm::length(location - m_camera.position()) - iconFadeStart) / (iconFadeEnd - iconFadeStart), 0.0f, 1.0f);
        const float scale = std::lerp(1.0f, iconMinScale, fade);
        const bool selected = editor.isSelected(entity);
        tint.w *= selected ? 1.0f : std::lerp(1.0f, iconMinAlpha, fade); // The selection never fades
        shown.push_back({
            .pick = { .id = registry.get<StableId>(entity), .position = center, .radius = iconRadius * theme::scale() * scale },
            .label = glyphs ? glyph : letter,
            .tint = tint,
            .scale = scale,
            .selected = selected,
        });
    };
    const auto lightTint = [](const glm::vec3& color) {
        const glm::vec3 tint = iconTint(color);
        return ImVec4{ tint.x, tint.y, tint.z, 1.0f };
    };
    for (const auto entity : registry.view<PointLight>())
        addIcon(entity, theme::icons::light, "P", lightTint(registry.get<PointLight>(world::Entity{ entity }).color));
    for (const auto entity : registry.view<DirectionalLight>())
        addIcon(entity, theme::icons::sun, "D", lightTint(registry.get<DirectionalLight>(world::Entity{ entity }).color));
    for (const auto entity : registry.view<Camera>())
        addIcon(entity, theme::icons::camera, "C", theme::cameraIcon);
    // The selected icon draws last, on top
    std::ranges::stable_partition(shown, [](const Icon& icon) { return !icon.selected; });

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(ImVec2{ rect.x, rect.y }, ImVec2{ rect.x + rect.width, rect.y + rect.height }, true);
    for (const auto& icon : shown)
    {
        const ImVec2 center{ icon.pick.position.x, icon.pick.position.y };
        const ImU32 color = theme::packed(icon.tint);
        ImVec4 backdrop = theme::iconBackdrop;
        backdrop.w *= icon.tint.w;
        drawList->AddCircleFilled(center, icon.pick.radius, theme::packed(backdrop));
        drawList->AddCircle(center, icon.pick.radius, icon.selected ? theme::packed(theme::iconSelected) : color, 0, (icon.selected ? 2.5f : 1.5f) * theme::scale());
        const ImVec2 textSize = ImGui::CalcTextSize(icon.label);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * icon.scale, ImVec2{ center.x - textSize.x * icon.scale * 0.5f, center.y - textSize.y * icon.scale * 0.5f }, color, icon.label);
        icons.push_back(icon.pick);
    }
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

auto Viewport::canPick(bool gizmo) const -> bool
{
    return m_hovered && !ImGui::GetIO().KeyAlt && !(gizmo && ImGuizmo::IsOver()) && !ImGui::IsAnyItemHovered();
}

auto Viewport::pickAt(const EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons, const glm::vec2& cursor) const -> std::optional<StableId>
{
    auto* world = editor.world();
    if (!world)
        return std::nullopt;
    if (const auto icon = pickIcon(cursor, icons))
        return icon;
    auto& registry = world->registry();
    std::vector<PickTarget> targets;
    for (const auto entity : registry.view<MeshRenderer, GlobalTransform, StableId>())
    {
        if (const auto bounds = meshBounds(registry, world::Entity{ entity }))
            targets.push_back({ .id = registry.get<StableId>(world::Entity{ entity }), .bounds = *bounds });
    }
    return pickNearest(m_camera.ray(toNdc(ImVec2{ cursor.x, cursor.y }, rect.x, rect.y, rect.width, rect.height), aspect()), targets);
}

void Viewport::pick(EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons, bool gizmo)
{
    // A plain left click on the image: not orbiting, not on this frame's gizmo or the toolbar
    const ImGuiIO& io = ImGui::GetIO();
    if (!canPick(gizmo) || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !editor.world())
        return;
    const auto picked = pickAt(editor, rect, icons, { io.MousePos.x, io.MousePos.y });

    // Ctrl+click adds or removes the entity, and keeps the selection when it hits nothing
    auto& selection = editor.selection();
    if (picked && io.KeyCtrl)
        selection.toggle(*picked);
    else if (picked)
        selection.select(*picked);
    else if (!io.KeyCtrl)
        selection.clear();
}

void Viewport::updateHover(const EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons, bool gizmo)
{
    // Nothing is hovered where a click wouldn't pick, or while a button is held: that is a drag, not a click
    if (!canPick(gizmo) || ImGui::IsAnyMouseDown())
    {
        m_hoveredId.reset();
        m_hoverCurrent = false;
        return;
    }
    // The CPU pick runs when the cursor or the wheel (a dolly) moved, not every frame
    const ImGuiIO& io = ImGui::GetIO();
    if (m_hoverCurrent && io.MouseDelta.x == 0.0f && io.MouseDelta.y == 0.0f && io.MouseWheel == 0.0f)
        return;
    m_hoveredId = pickAt(editor, rect, icons, { io.MousePos.x, io.MousePos.y });
    m_hoverCurrent = true;
}

void Viewport::drawBrackets(const EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons) const
{
    auto* world = editor.world();
    if (!world)
        return;
    auto& registry = world->registry();
    // What a click picks the entity by, on screen: its mesh's bounding sphere, else its icon. None while the sphere
    // reaches behind the camera, or for an entity with neither
    const auto box = [&](StableId id) -> std::optional<ImRect> {
        const auto entity = registry.find(id);
        if (!entity)
            return std::nullopt;
        if (const auto bounds = meshBounds(registry, entity))
        {
            const auto ndc = m_camera.projectSphere(*bounds, aspect());
            if (!ndc)
                return std::nullopt;
            return ImRect{ rect.x + (ndc->min.x + 1.0f) * 0.5f * rect.width, rect.y + (1.0f - ndc->max.y) * 0.5f * rect.height,
                rect.x + (ndc->max.x + 1.0f) * 0.5f * rect.width, rect.y + (1.0f - ndc->min.y) * 0.5f * rect.height };
        }
        const auto icon = std::ranges::find(icons, id, &PickIcon::id);
        if (icon == icons.end())
            return std::nullopt;
        const float extent = icon->radius + bracketIconGap * theme::scale();
        return ImRect{ icon->position.x - extent, icon->position.y - extent, icon->position.x + extent, icon->position.y + extent };
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(ImVec2{ rect.x, rect.y }, ImVec2{ rect.x + rect.width, rect.y + rect.height }, true);
    // The hovered entity's below the selection's, the active entity's on top
    const auto& selection = editor.selection();
    const auto active = selection.active();
    if (m_hoveredId && !selection.contains(*m_hoveredId))
    {
        if (const auto hovered = box(*m_hoveredId))
            drawCornerBrackets(drawList, *hovered, bracketHoverAlpha, bracketWidth);
    }
    for (const auto id : selection.ids())
    {
        if (id == active)
            continue;
        if (const auto selected = box(id))
            drawCornerBrackets(drawList, *selected, bracketSelectedAlpha, bracketWidth);
    }
    if (active)
    {
        if (const auto selected = box(*active))
            drawCornerBrackets(drawList, *selected, 1.0f, bracketActiveWidth);
    }
    drawList->PopClipRect();
}
}
