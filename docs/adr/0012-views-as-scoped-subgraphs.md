# Views render as scoped subgraphs of one frame graph

Decided 2026-11-17, see [#254](https://github.com/chFleschutz/aegis-engine/issues/254).

A frame renders a list of views. Each view's scene passes are added to the one frame graph again, inside a scope
of the view's name, so its resources ("Main/Depth", "Play/CameraData", its culling outputs) are its own, and its
textures follow the view's extent; only the instances are shared, written once by an unscoped pass. GPU-driven
culling runs once per view, with that view's camera. The graph is rebuilt when the list of view names changes and
resized per scope when a view's extent changes; a camera change needs neither.

Why: the planner keeps ordering and synchronizing every pass, so the barriers between views stay automatic and the
plan snapshots show them, and passes stay unaware of views (a scoped reference finds its scope's resource first).
The cost is a pass instance (and its pipelines) per view, and a sweep over the instances per view.

**Considered options**
- *Passes loop over views:* one pass records every view. Fewer passes, but each pass keeps per-view state and the
  barriers between views are written by hand, out of the planner's sight.
- *One culling dispatch for all views:* one sweep over the instances, but the shader, its buffers and the pass become
  view-count aware, and every view's culling is coupled into one pass.

→ shadow views (M12), the editor's edit and play side by side, and per-view history resources ([#253](https://github.com/chFleschutz/aegis-engine/issues/253)).
