module;

#include <cfloat>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.assets;
import :panels;
import :theme;

namespace aegis::editor
{
namespace
{
// The top-level window that 'window' belongs to: through a child window's parents and a popup's opener.
auto ownerWindow(const ImGuiWindow* window) -> const ImGuiWindow*
{
    while (window)
    {
        const ImGuiWindow* root = window->RootWindow;
        if (root->RootWindowPopupTree == root)
            return root;
        window = root->RootWindowPopupTree;
    }
    return nullptr;
}

// The right column's width in the default layout, at 100% scale.
constexpr float columnWidth = 360.0f;
// The width of the side rail the column folds into, at 100% scale.
constexpr float railWidth = 32.0f;

// The default layout in the dockspace 'dockspace' of 'size': one column on the right over the full height, the
// column panels 'above' (the hierarchy) over 'below' (the inspector), and the viewport in the rest. The column is about
// 360 px wide (scaled, at most 30% of the window) and keeps that width when the window is resized: ImGui gives the
// change to the central node. The drawer panels (assets, console, the debug panels) have no node: they open in the
// drawer, and a node below the viewport is split off when one is pinned (Workspace::pinNode).
void buildDefaultLayout(ImGuiID dockspace, ImVec2 size, const Panel* above, const Panel* below)
{
    constexpr float maxColumnShare = 0.3f;
    constexpr float hierarchyShare = 0.4f; // Of the column's height

    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);

    ImGuiID center = dockspace;
    const float columnShare = std::min(columnWidth * theme::scale() / std::max(size.x, 1.0f), maxColumnShare);
    ImGuiID inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, columnShare, nullptr, &center);
    const ImGuiID hierarchy = ImGui::DockBuilderSplitNode(inspector, ImGuiDir_Up, hierarchyShare, nullptr, &inspector);

    const auto dock = [](const std::string& window, ImGuiID node) {
        ImGui::DockBuilderDockWindow(window.c_str(), node);
        ImGui::DockBuilderGetNode(node)->SelectedTabId = ImGui::GetID(window.c_str());
    };
    if (above)
        dock(windowName(above->title()), hierarchy);
    if (below)
        dock(windowName(below->title()), inspector);
    dock("Viewport", center); // Its tab bar is hidden: no label of its own
    ImGui::DockBuilderFinish(dockspace);
}

// The share of the viewport's node the node of the pinned panels takes when it is split off.
constexpr float pinnedShare = 0.3f;

// Whether 'node' is the central node or holds it. By the node's flag, not the root's CentralNode, which the dockspace
// sets only once it is drawn: the first frame after the layout file was read needs the column too.
auto holdsCentral(const ImGuiDockNode* node) -> bool
{
    return node && (node->IsCentralNode() || holdsCentral(node->ChildNodes[0]) || holdsCentral(node->ChildNodes[1]));
}

// The right column of the dockspace 'dockspace': its root's right child when the root is split left and right and the
// central node is in the left one. Null otherwise (the user rearranged the layout), and before the first dockspace.
auto columnNode(ImGuiID dockspace) -> ImGuiDockNode*
{
    ImGuiDockNode* root = dockspace != 0 ? ImGui::DockBuilderGetNode(dockspace) : nullptr;
    if (!root || !root->IsSplitNode() || root->SplitAxis != ImGuiAxis_X)
        return nullptr;
    return holdsCentral(root->ChildNodes[0]) && !holdsCentral(root->ChildNodes[1]) ? root->ChildNodes[1] : nullptr;
}

// The window of the flyout that shows the column panel 'title'.
auto flyoutWindow(std::string_view title) -> std::string
{
    return std::format("{}##Flyout", title);
}

// Whether 'point' is inside 'rect'.
auto contains(const std::optional<Viewport::Rect>& rect, ImVec2 point) -> bool
{
    return rect && point.x >= rect->x && point.y >= rect->y && point.x < rect->x + rect->width && point.y < rect->y + rect->height;
}

// Whether the window 'title' floats now: it exists and is docked nowhere.
auto floats(std::string_view title) -> bool
{
    const std::string name = windowName(title);
    const ImGuiWindow* window = ImGui::FindWindowByName(name.c_str());
    return window && window->DockId == 0;
}

// The dock node the window 'title' is docked in, or was docked in last (its settings, before it is first drawn); 0 for none.
auto dockIdOf(std::string_view title) -> ImGuiID
{
    const std::string name = windowName(title);
    if (const ImGuiWindow* window = ImGui::FindWindowByName(name.c_str()))
        return window->DockId;
    const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(name.c_str()));
    return settings ? settings->DockId : 0;
}

// A docked, floating or dialog panel's window with its contents; its close button closes the panel. A dialog is centered
// as it appears and sized to its contents (or as wide as it asks), with its title and no saved layout.
void drawWindowPanel(Panel& panel, EditorContext& editor, FrameContext& frame)
{
    bool open = true;
    bool visible = false;
    if (panel.placement() == Placement::Dialog)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2{ 0.5f, 0.5f });
        if (const float width = panel.dialogWidth() * theme::scale(); width > 0.0f)
            ImGui::SetNextWindowSizeConstraints(ImVec2{ width, 0.0f }, ImVec2{ width, FLT_MAX });
        const std::string title{ panel.title() };
        visible = ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
    }
    else
        visible = ImGui::Begin(panel.label().c_str(), &open);
    panel.setOpen(open);
    if (visible)
        panel.draw(editor, frame);
    ImGui::End();
}

// A drawer panel's contents with the pin button over the right end of their first row, which the panel leaves free
// (Panel::trailingWidth): lit while 'pinned', where it unpins, and pins otherwise. Returns whether it was pressed.
auto drawWithPinButton(Panel& panel, EditorContext& editor, FrameContext& frame, bool pinned) -> bool
{
    const float button = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float right = start.x + ImGui::GetContentRegionAvail().x;
    panel.setTrailingWidth(button + ImGui::GetStyle().ItemSpacing.x);
    panel.draw(editor, frame);
    panel.setTrailingWidth(0.0f);
    ImGui::SetCursorScreenPos(ImVec2{ right - button, start.y });
    const bool pressed = theme::iconButton(theme::icons::pin, pinned ? "##unpin" : "##pin", pinned);
    ImGui::SetItemTooltip("%s", pinned ? "Unpin: back to the drawer" : "Pin: dock it below the viewport");
    return pressed;
}
}

Workspace::Workspace(EditorContext& editor, Viewport& viewport) :
    m_editor{ editor },
    m_viewport{ viewport }
{
}

void Workspace::add(std::unique_ptr<Panel> panel)
{
    if (panel->isOpen())
        m_defaultOpen.push_back(panel.get());
    m_panels.push_back(std::move(panel));
}

auto Workspace::find(std::string_view name) const -> Panel*
{
    const auto it = std::ranges::find(m_panels, name, [](const auto& panel) { return panel->name(); });
    return it == m_panels.end() ? nullptr : it->get();
}

void Workspace::open(std::string_view name)
{
    if (auto* panel = find(name))
        panel->setOpen(true);
}

void Workspace::openAll()
{
    std::string first;
    for (const auto& panel : m_panels)
    {
        if (panel->placement() == Placement::Dialog)
            continue;
        panel->setOpen(panel->placement() != Placement::Float); // Not All Entities: it floats over the viewport
        if (panel->placement() == Placement::Drawer)
        {
            pin(panel->name()); // The drawer shows only one
            if (first.empty())
                first = panel->name();
        }
    }
    m_focusPinned = first; // Its tab shows
}

void Workspace::addSettingsHandler()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "Aegis";
    handler.TypeHash = ImHashStr("Aegis");
    handler.UserData = this;
    handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* self, const char* name) -> void* {
        auto* workspace = static_cast<Workspace*>(self->UserData);
        const std::string_view section{ name };
        if (section == "Drawers")
            return &workspace->m_drawers;
        return section == "Rail" ? &workspace->m_rail : nullptr;
    };
    handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* self, void* entry, const char* line) {
        auto* workspace = static_cast<Workspace*>(self->UserData);
        if (entry == &workspace->m_drawers)
            workspace->m_drawers.readSettingsLine(line);
        else if (entry == &workspace->m_rail)
            workspace->m_rail.readSettingsLine(line);
    };
    handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* buffer) {
        const auto* workspace = static_cast<const Workspace*>(self->UserData);
        buffer->appendf("[Aegis][Drawers]\n%s\n\n", workspace->m_drawers.settingsLine().c_str());
        buffer->appendf("[Aegis][Rail]\n%s\n\n", workspace->m_rail.settingsLine().c_str());
    };
    ImGui::AddSettingsHandler(&handler);
}

void Workspace::removeSettingsHandler()
{
    ImGui::RemoveSettingsHandler("Aegis"); // It points at this workspace
}

void Workspace::showDrawer(std::string_view name)
{
    open(name);
    if (m_drawers.show(name))
        m_focusDrawer = true;
    else
        m_focusPinned = name; // Its tab shows
}

void Workspace::toggleDrawer(std::string_view name)
{
    open(name);
    if (!m_drawers.toggle(name))
        m_focusPinned = name; // Pinned: its tab shows
    else if (m_drawers.isShown(name))
        m_focusDrawer = true;
}

void Workspace::toggleFold()
{
    m_rail.toggleFolded();
    ImGui::MarkIniSettingsDirty();
}

void Workspace::foldInto(std::string_view name)
{
    const Panel* panel = find(name);
    m_rail.setFolded(true);
    if (panel)
        m_rail.show(panel->title());
    m_focusFlyout = true;
}

void Workspace::reveal(std::string_view name)
{
    Panel* panel = find(name);
    if (!panel)
        return;
    panel->setOpen(true);
    if (m_fullViewport.active())
    {
        m_fullViewport.leave();
        syncFullViewport();
    }
    if (inFoldedColumn(panel->title()) && m_rail.show(panel->title()))
        m_focusFlyout = true;
}

auto Workspace::hovers(std::string_view name) const -> bool
{
    const Panel* panel = find(name);
    const ImGuiWindow* hovered = ImGui::GetCurrentContext()->HoveredWindow;
    const ImGuiWindow* root = hovered ? hovered->RootWindow : nullptr;
    return panel && root && (root->ID == ImHashStr(windowName(panel->title()).c_str()) || root->Name == flyoutWindow(panel->title()));
}

void Workspace::toggleFullViewport()
{
    m_fullViewport.toggle();
    syncFullViewport();
}

void Workspace::play(bool onPlay)
{
    m_fullViewport.play(onPlay);
    syncFullViewport();
}

void Workspace::stop()
{
    m_fullViewport.stop();
    syncFullViewport();
}

void Workspace::syncFullViewport()
{
    const bool full = m_fullViewport.active();
    if (full == m_viewport.fullWindow())
        return;
    if (full)
    {
        m_drawers.close(); // Esc would have to close it before it leaves full viewport
        m_rail.close();    // Likewise; the rail is hidden
    }
    m_viewport.setFullWindow(full);
}

void Workspace::resetLayout()
{
    m_resetLayout = true;
    for (auto* panel : m_defaultOpen)
        panel->setOpen(true);
    m_drawers.reset();
    m_rail.setFolded(false);
    m_dockRequests.clear();
    m_pinNode = 0;
    ImGui::MarkIniSettingsDirty();
}

void Workspace::drawLayout(FrameContext& frame)
{
    const bool full = m_fullViewport.active();
    drawDockspace();
    for (const auto& panel : m_panels)
    {
        const Placement placement = panel->placement();
        if (!panel->isOpen() || (full && (placement == Placement::Float || placement == Placement::Dialog || floats(panel->title()))))
            continue;
        if (placement == Placement::Column)
            drawColumnPanel(*panel, frame);
        else if (placement != Placement::Drawer)
            drawWindowPanel(*panel, m_editor, frame);
        else if (m_drawers.isPinned(panel->name()))
            drawPinned(*panel, frame);
    }
}

void Workspace::drawOverlays(FrameContext& frame)
{
    const bool drawerWasShown = m_drawers.shown().has_value();
    drawDrawer(frame); // Over the viewport, whose image it is placed by
    drawFlyout(frame, drawerWasShown);
    leaveFullViewportOnEscape(drawerWasShown);
}

void Workspace::drawDockspace()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menuBar = theme::menuBarHeight();
    const float statusBar = theme::statusBarHeight();
    const ImGuiID id = ImGui::GetID("EditorDockSpace");
    m_dockspace = id;
    // Folded, the rail takes the right edge, and the dockspace is wider than the rest by the column's width: the column
    // lies off screen at its width, its panels docked in it (a docked window that isn't submitted leaves its node), and
    // ImGui gives the width it gained to the central node, the viewport's. Unfolded, the layout is as it was.
    float width = viewport->Size.x;
    if (m_rail.folded())
    {
        width -= std::round(railWidth * theme::scale());
        if (const ImGuiDockNode* column = columnNode(id))
            width += column->Size.x;
    }
    const ImVec2 size{ std::max(width, 1.0f), std::max(viewport->Size.y - menuBar - statusBar, 1.0f) };
    ImGui::SetCursorPos(ImVec2{ 0.0f, menuBar });
    if (std::exchange(m_resetLayout, false) || !ImGui::DockBuilderGetNode(id))
    {
        // The column panels top to bottom, in the order they were added
        std::array<const Panel*, 2> column{};
        auto next = column.begin();
        for (const auto& panel : m_panels)
        {
            if (panel->placement() == Placement::Column && next != column.end())
                *next++ = panel.get();
        }
        buildDefaultLayout(id, size, column[0], column[1]);
    }
    dockPinned(); // Before the dockspace lays out its nodes, so a pinned panel shows docked at once
    // The panels' tabs have their own close buttons, and the View menu lists them: no window menu or close-all per node
    ImGui::DockSpace(id, size, ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton);
}

auto Workspace::inFoldedColumn(std::string_view title) const -> bool
{
    ImGuiDockNode* column = m_rail.folded() ? columnNode(m_dockspace) : nullptr;
    const ImGuiID id = column ? dockIdOf(title) : 0;
    ImGuiDockNode* node = id != 0 ? ImGui::DockBuilderGetNode(id) : nullptr;
    return node && ImGui::DockNodeIsInHierarchyOf(node, column);
}

auto Workspace::findColumnPanel(std::string_view title) const -> Panel*
{
    const auto it = std::ranges::find_if(m_panels, [title](const auto& panel) { return panel->placement() == Placement::Column && panel->title() == title; });
    return it == m_panels.end() ? nullptr : it->get();
}

void Workspace::toggleFlyout(std::string_view title)
{
    auto* panel = findColumnPanel(title);
    if (!panel)
        return;
    panel->setOpen(true);
    if (!inFoldedColumn(title))
    {
        ImGui::SetWindowFocus(windowName(title).c_str()); // Also selects its tab
        return;
    }
    if (m_rail.toggle(title) && m_rail.isShown(title))
        m_focusFlyout = true;
}

void Workspace::drawRail()
{
    if (!m_rail.folded())
        return;
    // Between the menu bar and the status bar, over the column's edge, which lies below it off screen
    const ImGuiViewport* display = ImGui::GetMainViewport();
    const float menuBar = theme::menuBarHeight();
    const float width = std::round(railWidth * theme::scale());
    ImGui::SetNextWindowPos(ImVec2{ display->Pos.x + display->Size.x - width, display->Pos.y + menuBar });
    ImGui::SetNextWindowSize(ImVec2{ width, std::max(display->Size.y - menuBar - theme::statusBarHeight(), 1.0f) });
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    const float padding = std::max(std::floor((width - ImGui::GetFrameHeight()) * 0.5f), 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ padding, padding });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bgDeep);
    const bool visible = ImGui::Begin("##Rail", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (visible)
    {
        // A button per column panel, top to bottom, with its tab's icon
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
        for (const auto& panel : m_panels)
        {
            if (panel->placement() != Placement::Column)
                continue;
            const std::string title{ panel->title() };
            if (theme::iconButton(panel->icon(), title.c_str(), m_rail.isShown(title)))
                toggleFlyout(title);
            ImGui::SetItemTooltip("%s", title.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

void Workspace::drawFlyout(FrameContext& frame, bool drawerWasShown)
{
    if (const auto shown = m_rail.flyout())
    {
        const std::string title{ *shown };
        const Panel* panel = findColumnPanel(title);
        // Esc closes the drawer first; losing the focus doesn't close it (popups and drags take it)
        const bool escape = !drawerWasShown && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput && !ImGui::GetDragDropPayload() &&
                            !m_viewport.givesInputToGame() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        // The Hierarchy is for picking: a left click into the viewport closes it once the viewport picked (flying and
        // panning don't). The Inspector stays and follows the selection.
        const bool click = panel && panel->name() == panels::scene && m_viewport.hovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (!panel || !panel->isOpen() || !inFoldedColumn(title) || m_fullViewport.active() || escape || click)
            m_rail.close();
    }
    const auto shown = m_rail.flyout();
    const auto image = m_viewport.imageRect();
    if (!shown || !image)
    {
        m_focusFlyout = false;
        return;
    }
    const std::string title{ *shown };
    Panel* panel = findColumnPanel(title);

    // Over the right edge of the image, as tall, and as wide as the column
    const ImGuiDockNode* column = columnNode(m_dockspace);
    const float width = std::min(column ? column->Size.x : std::round(columnWidth * theme::scale()), image->width);
    ImGui::SetNextWindowPos(ImVec2{ image->x + image->width - width, image->y });
    ImGui::SetNextWindowSize(ImVec2{ width, image->height });
    if (std::exchange(m_focusFlyout, false))
        ImGui::SetNextWindowFocus();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;
    if (ImGui::Begin(flyoutWindow(title).c_str(), nullptr, flags))
        panel->draw(m_editor, frame);
    ImGui::End();
}

void Workspace::leaveFullViewportOnEscape(bool drawerWasShown)
{
    // The same Esc as the drawer's: the first one closes the drawer, a popup takes its own
    if (!m_fullViewport.active() || drawerWasShown || !ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        return;
    if (ImGui::GetIO().WantTextInput || ImGui::GetDragDropPayload() || m_viewport.givesInputToGame() || m_viewport.cameraDragged() || ImGuizmo::IsUsing() ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;
    m_fullViewport.leave();
    syncFullViewport();
}

void Workspace::pin(std::string_view name)
{
    m_drawers.pin(name);
    if (std::ranges::find(m_dockRequests, name) == m_dockRequests.end())
        m_dockRequests.emplace_back(name);
    m_focusPinned = name;
    ImGui::MarkIniSettingsDirty();
}

void Workspace::unpin(std::string_view name)
{
    m_drawers.unpin(name);
    m_focusDrawer = true;
    ImGui::MarkIniSettingsDirty();
}

auto Workspace::pinNode(std::string_view name) -> std::uint32_t
{
    // A leaf of the layout's tree that is not the viewport's; one that lost its windows stays, hidden, until reused
    const auto usable = [this](ImGuiID id) {
        ImGuiDockNode* node = id != 0 ? ImGui::DockBuilderGetNode(id) : nullptr;
        return node && node->IsLeafNode() && !node->IsCentralNode() && ImGui::DockNodeGetRootNode(node)->ID == m_dockspace;
    };
    if (const ImGuiID own = dockIdOf(name); usable(own))
        return own;
    for (const auto& other : m_drawers.pinned())
    {
        if (const ImGuiID id = dockIdOf(other); other != name && usable(id))
            return id;
    }
    if (usable(m_pinNode))
        return m_pinNode;
    ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(m_dockspace);
    if (!central)
        return 0;
    ImGuiID rest = 0;
    return ImGui::DockBuilderSplitNode(central->ID, ImGuiDir_Down, pinnedShare, nullptr, &rest); // The viewport stays central
}

void Workspace::dockPinned()
{
    if (m_dockRequests.empty())
        return;
    for (const auto& name : std::exchange(m_dockRequests, {}))
    {
        if (!m_drawers.isPinned(name))
            continue;
        if (const ImGuiID node = pinNode(name); node != 0)
        {
            ImGui::DockBuilderDockWindow(windowName(name).c_str(), node);
            m_pinNode = node;
        }
    }
    ImGui::DockBuilderFinish(m_dockspace);
}

void Workspace::drawPinned(Panel& panel, FrameContext& frame)
{
    const std::string name{ panel.name() };
    if (m_focusPinned == name)
    {
        ImGui::SetNextWindowFocus(); // Also selects its tab
        m_focusPinned.clear();
    }
    bool open = true;
    const bool visible = ImGui::Begin(panel.label().c_str(), panel.menu() == Menu::Debug ? &open : nullptr); // Content and the console stay open
    if (!open)
        panel.setOpen(false);
    // Appearing undocked (pinned in the layout file without a node, or dragged out to float before): docked below the
    // viewport before the next frame's dockspace. Only as it appears: a drag to another node undocks it meanwhile.
    if (ImGui::IsWindowAppearing() && !ImGui::IsWindowDocked() && std::ranges::find(m_dockRequests, name) == m_dockRequests.end())
        m_dockRequests.push_back(name);
    if (visible && drawWithPinButton(panel, m_editor, frame, true))
        unpin(name);
    ImGui::End();
}

void Workspace::drawColumnPanel(Panel& panel, FrameContext& frame)
{
    const std::string title{ panel.title() };
    // Folded: submitted empty, off screen, so it keeps its dock node; the flyout draws its contents
    const bool folded = inFoldedColumn(title);
    if (ImGui::Begin(panel.label().c_str()) && !folded)
        panel.draw(m_editor, frame);
    ImGui::End();
}

void Workspace::drawDrawer(FrameContext& frame)
{
    const ImGuiIO& io = ImGui::GetIO();
    // A drag that began in the drawer closes it once it leaves it. ImGui keeps the payload while the button is held,
    // though its source is no longer drawn, so the drop still lands in the viewport or on an inspector slot.
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload)
        m_dragFromDrawer = m_dragHidDrawer = false;
    else if (!m_dragHidDrawer && contains(m_drawerRect, io.MouseClickedPos[ImGuiMouseButton_Left]))
        m_dragFromDrawer = true;
    if (m_dragFromDrawer && m_drawers.shown() && !contains(m_drawerRect, io.MousePos))
    {
        m_drawers.close();
        m_dragFromDrawer = false;
        m_dragHidDrawer = true;
    }

    // Esc and a click outside it close it: anywhere but the drawer, its popups (a menu, the type filter) and the status
    // bar, whose buttons switch it. Losing the focus alone doesn't: drag and drop takes it.
    if (m_drawers.shown())
    {
        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput && !payload && !m_viewport.givesInputToGame() &&
                            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const ImGuiWindow* owner = ownerWindow(ImGui::GetCurrentContext()->HoveredWindow);
        const bool inside = owner && (owner->ID == ImHashStr("##Drawer") || owner->ID == ImHashStr(statusBarWindow));
        const bool click = !inside && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle));
        if (escape || click)
            m_drawers.close();
    }

    const auto shown = m_drawers.shown();
    const Panel* panel = shown ? find(*shown) : nullptr;
    if (shown && (!panel || !panel->isOpen()))
        m_drawers.close();
    const auto viewport = m_viewport.imageRect();
    if (!m_drawers.shown() || !viewport)
    {
        m_drawerRect.reset();
        m_focusDrawer = false;
        return;
    }

    // Over the bottom of the image and as wide: the render extent stays, and the right column stays usable
    const float height = std::clamp(std::round(viewport->height * m_editor.preferences().drawerHeight), std::min(4.0f * ImGui::GetFrameHeight(), viewport->height), viewport->height);
    const Viewport::Rect rect{ .x = viewport->x, .y = viewport->y + viewport->height - height, .width = viewport->width, .height = height };
    m_drawerRect = rect;
    ImGui::SetNextWindowPos(ImVec2{ rect.x, rect.y });
    ImGui::SetNextWindowSize(ImVec2{ rect.width, rect.height });
    if (std::exchange(m_focusDrawer, false))
        ImGui::SetNextWindowFocus();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##Drawer", nullptr, flags))
    {
        drawDrawerResize(*viewport, rect.y);

        // The shown panel with no header: the status bar's lit button names it
        bool pinShown = false;
        const auto now = m_drawers.shown();
        if (auto* contents = now ? find(*now) : nullptr; contents && contents->isOpen())
        {
            const std::string id{ *now };
            ImGui::PushID(id.c_str());
            // In the child, which takes the hover from the drawer's window beneath it
            if (ImGui::BeginChild("##contents"))
                pinShown = drawWithPinButton(*contents, m_editor, frame, false);
            ImGui::EndChild();
            ImGui::PopID();
        }
        if (pinShown && now)
            pin(std::string{ *now }); // A copy: pinning closes the drawer, and with it the name it shows
    }
    ImGui::End();
}

void Workspace::drawDrawerResize(const Viewport::Rect& viewport, float top)
{
    // A strip along the top edge, inside the window's padding, where ImGui would clip it
    const float grip = 4.0f * theme::scale();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 position = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();
    ImGui::PushClipRect(position, ImVec2{ position.x + width, position.y + grip }, false);
    ImGui::SetCursorScreenPos(ImVec2{ position.x, top });
    ImGui::InvisibleButton("##resize", ImVec2{ width, grip });
    // The edge is the border's line, the accent's while it can be dragged
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (active || hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    const ImVec4 color = active ? theme::accentActive : (hovered ? theme::accent : theme::border);
    ImGui::GetWindowDrawList()->AddLine(ImVec2{ position.x, top + 1.0f }, ImVec2{ position.x + width, top + 1.0f }, theme::packed(color), 2.0f * theme::scale());
    if (active)
    {
        const float share = (viewport.y + viewport.height - ImGui::GetIO().MousePos.y) / viewport.height;
        m_editor.preferences().drawerHeight = std::clamp(share, minDrawerHeight, maxDrawerHeight);
    }
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(cursor);
}

void Workspace::drawDragTooltip(FrameContext& frame)
{
    // The asset's source went with the drawer, and its tooltip with it
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!m_dragHidDrawer || !payload || !payload->IsDataType(assetPayloadType) || payload->DataSize != static_cast<int>(sizeof(AssetPayload)))
        return;
    AssetPayload asset;
    std::memcpy(&asset, payload->Data, sizeof(asset));
    const auto* entry = frame.assets.database().find(assets::AssetId{ asset.id });
    if (!entry)
        return;
    const auto name = entry->key.empty() ? std::filesystem::path{ entry->path }.filename().string() : entry->key;
    ImGui::SetTooltip("%s", std::format("{} ({})", name, asset.assetType()).c_str());
}
}
