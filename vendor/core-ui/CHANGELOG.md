# Changelog

## 1.7.0 build 253 - Opacity layer shadow bounds

### Rendering

Opacity transitions now expand their D2D layer bounds to include outset
box-shadows. Toolbars and other translucent widgets with soft shadows fade in
with their shadow instead of clipping the shadow until the opacity animation
finishes.

## 1.7.0 build 252 - Swapchain resize background color

### Rendering

HWND swapchains now keep their DXGI background color synchronized with the
frame's initial clear color. During live window resizing, DXGI uses that color
for target areas outside the current back buffer, so newly exposed resize
strips match the window background instead of flashing black before the resized
frame is presented.

## 1.7.0 build 251 - True blurred CSS box shadows

### Rendering

CSS `box-shadow` now renders blurred outset shadows through a D2D Gaussian blur
mask instead of stacking visible translucent rounded rectangles. This produces a
continuous blur for large shadows such as floating toolbars, while keeping the
old layered approximation only as a fallback when the D2D effect path is
unavailable.

## 1.7.0 build 250 - Draw.io SVG HTML labels

### Rendering

Draw.io / diagrams.net SVG labels exported as XHTML `foreignObject` content
inside `<switch>` blocks are now converted into DWrite glyph paths before the
D2D SVG document is created. The converter handles the draw.io label subset
used by circuit and diagram exports, including nested `font`, `i`, `b`, `sub`,
`sup`, color, font family, font size, line height, and the outer flex-positioned
label box.

The converted label replaces the whole draw.io `<switch>` block so D2D no
longer drops the text when it encounters unsupported HTML `foreignObject`
content.

## 1.7.0 build 249 - Optional independent submenu widths

### UIX

`<menu>` now accepts `share-width="false"` / `shareWidth="false"` to opt a
menu tree out of the default parent/submenu width sharing. This lets compact
context menus keep their submenus compact even when the root menu has shortcut
or submenu-arrow columns.

## 1.7.0 build 248 - Menu row class overrides

### UIX

`<menu>` now accepts `row-class` / `rowClass`, which appends an extra class to
the generated `.menuitem-row` wrapper for every item in that menu tree. This
lets callers tune one context menu's content width without changing unrelated
menus that share the same page stylesheet.

## 1.7.0 build 247 - Simple SVG alpha masks on D2D

### Rendering

Simple SVG alpha masks made from a single solid geometry element are now
converted to equivalent `clipPath` references before D2D SVG rendering. This
keeps icons such as Chrome-style circular artwork on the crisp D2D vector path
instead of ignoring the mask and drawing their color wedges as rectangles.

Complex masks that depend on gradients, partial opacity, images, filters, or
other non-binary alpha content are not converted; those remain routed through
the safer raster fallback path when needed.

## 1.7.0 build 246 - SVG GaussianBlur raster antialiasing

### Rendering

Small SVGs that route through the LunaSVG GaussianBlur main renderer now use a
higher supersampling floor before being downsampled for display. This reduces
visible stair-stepping on circular icon edges while keeping the ordinary D2D
vector SVG path unchanged.

## 1.7.0 build 245 - Ordered SVG GaussianBlur filters

### Rendering

SVG files containing `feGaussianBlur` filters now route through the LunaSVG main
renderer, where filter effects are applied at each filtered element's offscreen
composition step. This preserves SVG document order for foreground glows and
SourceAlpha-based soft shadow chains, including cases where a background would
otherwise cover a D2D compensation layer.

LunaSVG filter support now covers blur-only `feGaussianBlur`, `feDropShadow`,
and common `SourceAlpha -> feOffset -> feGaussianBlur -> feColorMatrix` shadow
chains. Build 244's D2D layer extraction remains useful for `feDropShadow`, but
was not sufficient for order-sensitive GaussianBlur artwork.

## 1.7.0 build 244 - SVG GaussianBlur filter layers

### Rendering

SVG filter replay now recognizes blur-only `feGaussianBlur` filters and common
exporter shadow chains built from `SourceAlpha`, `feOffset`, `feGaussianBlur`,
`feColorMatrix`, and `feBlend`. These filters are converted into D2D blur
layers matched by each element's `filter="url(#id)"`, so glow effects and
non-`feDropShadow` soft shadows no longer disappear in the main image view.

Filter compensation layers are now drawn before the stripped base SVG, keeping
blurred glows and shadows behind later SVG content instead of covering it.

## 1.7.0 build 243 - SVG drop-shadow gradient references

### Rendering

SVG drop-shadow replay now preserves top-level resource definitions when
building per-element shadow and cover documents. Filtered shapes that use
`fill="url(#gradient)"`, clip paths, masks, patterns, or similar defs-backed
references no longer fall back to black when the cover layer is redrawn after
the shadow blur.

## 1.7.0 build 242 - Independent menu rendering

### UI

Context menu popups no longer register layered render windows or submit frames
to the global render thread. Menus now create their own DirectComposition-backed
render target on the overlay thread and repaint there for hover, resize,
submenu, and debug screenshot flows.

This completes the menu side of the overlay split: the menu window, timer,
input handling, and rendering are no longer queued behind main-window render
thread work.

## 1.7.0 build 241 - Independent overlay service

### UI

Toast rendering now runs through an independent overlay service instead of the
main window render path. Toast windows, timers, and layered-window updates stay
responsive while the host window is busy with heavy image loading or GPU upload.

Context menu popup windows are also hosted by the same overlay service, removing
the separate popup window-class path and giving future floating UI components a
single host-window/thread foundation.

## 1.7.0 build 240 - IconButton theme color roles

### Controls

IconButton now supports theme color roles through
`ui_icon_button_set_icon_color_role`. Callers can bind icons to semantic theme
colors such as button text, titlebar text, content text, accent, danger, and
divider while preserving runtime light/dark theme switching. Fixed RGBA colors
remain available through `ui_icon_button_set_icon_color`.

## 1.7.0 build 239 - Shared D2D serialization

### Rendering

Render-thread frame replay, prepared presents, screenshots, and SVG off-screen
thumbnail rendering now share a narrow D2D/D3D guard. This prevents rapid image
switching from concurrently entering the shared single-threaded Direct2D device
from the UI thread and the render thread, which could crash inside D2D/AMD
drivers when SVG documents were involved.

## 1.7.0 build 238 - UIX anchor placement helpers

### UIX runtime

UIX scripts can now query settled widget geometry with `this.$rect(id)`, read
the current content size with `this.$windowSize()`, and run a settled callback
with `this.$nextTick(fn)`. Pages may also define `onResize(width, height)` to
react to window resizes without taking over the host application's resize
callback.

### Layout

Reactive bindings now support `:left`, `:top`, `:right`, and `:bottom` for
absolute-positioned widgets. These dynamic side values survive hover/class
style recomputation, so script-positioned popovers stay anchored after normal
state changes.

## 1.7.0 build 237 - Borderless DWM-coordinated visual transaction

### Rendering

Canvas visual transactions that resize a borderless HWND now prepare the target
render-thread frame without presenting it, notify resize callbacks at the target
client size for the prepared frame, wait for a DWM composition boundary, commit
the real `SetWindowPos`, and then present the prepared back buffer from the
render thread. This keeps programmatic borderless zoom closer to one DWM
composition interval while preserving the render-thread-only present path.

## 1.7.0 build 236 - Visual transaction present barrier

### Rendering

Build numbers now continue from the 1.6.5 line. Canvas visual transactions no
longer submit an intermediate frame from `ui_window_set_rect`; resize and paint
invalidations stay deferred until the outermost transaction ends. Visual
transaction frames are also tagged explicitly and wait for the render thread to
complete that generation before control returns, so borderless canvas workflows
do not expose a resized HWND with stale image content.

## 1.7.0 build 1 - Minimap resource recording

### Rendering

Custom widget draw callbacks now write directly into the active display-list
recorder when a frame is being recorded. This keeps `ui_draw_bitmap` resources
owned by the final frame list instead of a temporary local list, restoring
GuoheView minimap thumbnails under render-thread replay. SVG minimap rendering
also prefers the D2D off-screen document path so drop-shadow/filter layers match
the main `gh_img_view` display, with LunaSVG kept as a fallback.

## 1.7.0 build 0 - Render-thread foundation

### Release

Core UI 1.7.0 rolls up the render-thread presentation migration from the
1.6.5 line: normal windows, layered popups, screenshots, visual transactions,
ResourceStore-backed image paths, and device-lost recovery now share the
render-thread pipeline. Legacy visual-update aliases and GPU-only image input
paths were already removed in 1.6.5 build 234, so 1.7.0 is the new clean
baseline for downstream applications.

## 1.6.5 build 235 - Render-thread device recovery

### Rendering

Render-thread device loss now has a closed recovery path: failed render frames
clear render-window GPU resources, reset the shared D3D/D2D device, wake
generation waiters, and ask visible UI windows to submit a final recovery
frame. ResourceStore budget enforcement also runs when visible resources are
unpinned, so evictable CPU resources cannot stay over budget until the next
write. Added regression tests for device recovery requests and ResourceStore
budget eviction.

## 1.6.5 build 234 - Remove legacy rendering compatibility entry points

### Rendering

The old `ui_window_begin_visual_update` / `ui_window_end_visual_update`
aliases have been removed; callers must use the explicit
CanvasVisualTransaction API. GPU-only `ID2D1Bitmap` input paths were also
removed from ImageView/ImageViewPlus internals, and `ui_image_get_pixels()`
now reads from the CPU ResourceStore backing instead of copying from a D2D
bitmap. This keeps image data ownership on the render-thread-safe resource
path.

## 1.6.5 build 233 - Render-thread visual transaction scheduling

### Rendering

FrameScheduler now has explicit visual-transaction request helpers, and
borderless canvas transactions flush through the scheduler before building a
frame. Programmatic borderless resize/zoom transactions no longer call
`PaintAndValidate()` without a pending frame, preventing a zero-generation
no-op where the HWND changed size before the image frame was submitted to the
render thread.

## 1.6.5 build 232 - Mermaid SVG foreignObject backgrounds

### SVG

Mermaid XHTML label `background-color` rules are now preserved during SVG style
inlining and emitted as SVG background rectangles when `<foreignObject>` labels
are converted into glyph paths. Edge labels regain their browser-like grey
backplates while text still uses the corrected dark fill and stroke isolation
from builds 230 and 231.

## 1.6.5 build 231 - Mermaid SVG foreignObject text color

### SVG

Converted Mermaid `<foreignObject>` labels now prefer XHTML/CSS `color` when
choosing the glyph path fill. This keeps labels dark even when Mermaid node
styles apply the node fill color to label containers, while build 230's
`stroke="none"` isolation still prevents dashed node strokes from fragmenting
text outlines.

## 1.6.5 build 230 - Mermaid SVG text stroke isolation

### SVG

Glyph paths generated from SVG `<text>` and Mermaid-style `<foreignObject>`
labels now explicitly disable inherited strokes. This prevents node styles such
as `stroke-dasharray:5 5` from being applied to converted text outlines, fixing
broken / fragmented labels in Mermaid flowcharts.

## 1.6.5 build 229 - Mermaid SVG presentation attributes

### SVG

Resolved SVG CSS values are now mirrored into presentation attributes such as
`fill`, `stroke`, and `stroke-width` after style inlining. This prevents D2D
from letting original attributes like `fill="none"` override Mermaid's
`!important` CSS rules, restoring solid fills for Mermaid rough / hand-drawn
flowchart nodes.

## 1.6.5 build 228 - Mermaid SVG CSS inlining

### SVG

SVG style inlining now uses the shared CSS parser and selector matcher for
tag, class, id, descendant, child, selector-list, specificity, source-order, and
`!important` handling. Mermaid exports that style nodes with rules such as
`#id .node rect` and `.startEnd>*` now receive resolved inline `fill` and
`stroke` values before entering the D2D SVG path, preventing nodes from falling
back to black.

## 1.6.5 build 227 - popup render-thread factory binding

### Render thread

Render-thread replay now binds `Renderer`'s active D2D factory to the factory
owned by the shared D2D device before creating window targets. This prevents
`D2DERR_WRONG_FACTORY` when popup menus replay rounded clips, SVG icons, or
other geometry commands on the render thread, restoring visible context menus
after the UI-thread popup renderer path was removed.

## 1.6.5 build 226 - SVG drop-shadow display-list replay

### SVG

`gh_img_view` now records D2D drop-shadow layer XML into the display list when an
SVG uses `feDropShadow`. Render-thread replay recreates the shadow and cover
documents, applies the Gaussian blur, and draws the source cover on top. This
restores filtered SVG logos after build 225 allowed SVG loading without a
UI-thread render target.

## 1.6.5 build 225 - SVG load after render-thread handoff

### Render thread

`gh_img_view` no longer requires the UI-thread render target while loading SVG
files. SVG XML is validated through a temporary D2D context when needed and then
recorded into the display list, allowing the render thread to recreate the D2D
SVG document during replay. This fixes `ui_gh_img_view_set_svg_file` returning
`-3` after the UI-thread synchronous render target path was removed.

## 1.6.5 build 224 - recorded glyph icons

### Render thread

`Renderer::DrawIcon` now records Segoe MDL2 glyph icons through the display-list
text path, preserving the requested font family during replay. This restores
caption buttons and other font-glyph icons after the UI-thread synchronous
render target path was removed.

## 1.6.5 build 223 - render-thread cleanup

### Render thread

`gh_img_view` no longer creates D2D bitmaps when hosts submit preview or tile
pixels. Preview, SVG raster, and tile drawing now go through CPU resource keys;
render-thread replay uploads only the resources referenced by the current frame
and prunes GPU bitmaps that are no longer visible.

The main window paint path now records display lists for render-thread
submission instead of falling back to UI-thread `BeginDraw` / `EndDraw` /
`Resize` paths. The old visual resize prepaint, `DwmFlush`, and back-buffer
present handoff have been removed, and renderer targets now assert debug builds
when used from the wrong owning thread.

## 1.6.5 build 222 - render-thread frame recording

### Render thread

Display-list recording now covers the remaining high-risk render-thread paths:
GIF frames are stored as CPU resources, loading spinners record line segments,
TextArea text uses recorded draw commands, CSS gradients replay from display
commands, and inset shadows no longer depend on a UI-thread render target.

Resize handling now submits a final frame when interactive sizing exits, so the
render thread receives an explicit final resize job after the transient sizing
policy ends.

### Window

Added `ui_window_begin_canvas_visual_transaction` and
`ui_window_end_canvas_visual_transaction` for host-side borderless canvas updates.
The old visual-update entry points remain as compatibility wrappers.

## 1.6.5 build 221 - live input repaint

### Window

Mouse and keyboard input dispatch now commits visual changes for the current
window immediately when hover, press, release, shortcut, or host-command paths
change UI state. This makes toolbar buttons, caption buttons, hover states, and
host-driven commands repaint without waiting for a later click or deferred
`WM_PAINT`, while background invalidation paths remain asynchronous.

## 1.6.5 build 220 - live wheel repaint

### Window

Mouse-wheel dispatch now requests an immediate paint for the current window
when a wheel hook fires or a built-in widget consumes the wheel event. This
keeps hook-driven zoom and scroll updates visible during wheel input instead
of delaying them until a later click or paint opportunity.

## 1.6.5 build 219 - no-activate immediate show

### Window

Added `ui_window_show_immediate_no_activate`, an immediate show path for owned
tool/info windows that need to render and become visible without taking keyboard
focus from the current foreground window. Existing `ui_window_show_immediate`
keeps its activating behavior.

## 1.6.5 build 218 - cancellable window close request

### Window

Added `ui_window_on_close_request`, a cancellable close-request callback for
`WM_CLOSE` sources such as titlebar close and Alt+F4. Returning zero lets hosts
handle the request themselves, for example by hiding and reusing a prepared
window, while existing `ui_window_on_close` remains a pre-destroy notification.

## 1.6.5 build 217 - live drag repaint

### Window

Pressed widgets that handle mouse-move during capture now request an immediate
paint for their own window after invalidation. This prevents continuous drag
messages from delaying visual updates until the mouse stops.

### GhImgView

Canvas panning no longer invalidates every top-level window. Drag repaint is
owned by the dispatching window, avoiding unnecessary repaint work in owned
windows such as image-info or settings panels.

## 1.6.5 build 216 - borderless mouse activation

### Window

Borderless windows now explicitly activate on mouse input before core-ui
consumes custom non-client paths such as canvas drag tracking. This keeps real
keyboard shortcuts working after users click back into a canvas-mode window
from another application, while preserving custom overscreen dragging.

## 1.6.5 build 215 - canvas render-target safe tracking

### Window

Canvas-mode max tracking is capped below the Direct3D render-target dimension
limit, avoiding invalid giant swapchain/render-target sizes during aggressive
borderless image zooming.

Borderless hit testing now falls back to the client rectangle when the render
target is temporarily unavailable, so the entire window is not misclassified as
a resize corner.

## 1.6.5 build 214 - canvas overscreen window tracking

### Window

Canvas-mode borderless windows now set an explicit large max-track size, so
programmatic resize and user tracking are not capped to the monitor work area.

Canvas-mode dragging now uses core-ui's own capture + `SetWindowPos` path
instead of the Windows system move loop. This lets callers drag borderless
image windows past screen edges without Aero snap or work-area clamping.

## 1.6.5 build 213 - dynamic menu background binding

### Menu

`<menu>` now accepts bound `:background`, `:background-color`, `:bg-color`,
and `:bgColor` attributes. They are evaluated before each popup show and share
the same CSS color parser as the existing static menu background attributes.

## 1.6.5 build 212 - frosted menu opacity

### Menu

The default frosted menu card alpha is now `0.75` in both light and dark
themes. Menus still render as opaque theme cards when frosted material is
disabled.

## 1.6.5 build 211 - menu material switch

### Menu

Menus now have an explicit frosted-material switch via C API
`ui_menu_set_frosted_material` and `.uix` static/bound `frosted-material` /
`:frosted-material`. `backdrop-blur` now only controls material blur strength;
disabling material uses an opaque theme card and skips backdrop capture.

## 1.6.5 build 210 - dynamic menu material binding

### Menu

`<menu>` now accepts bound `:backdrop-blur` / `:backdrop-filter` attributes.
The expression is evaluated before each popup show, so hosts can drive menu
frosted-glass material from reactive page state while preserving the existing
static `backdrop-blur` fallback.

## 1.6.5 build 209 - declarative menu material

### Menu

`<menu>` in `.uix` now accepts `background` / `background-color` / `bg-color`
and `backdrop-blur` / `backdrop-filter` static attributes. These map to the
existing `ContextMenu` material controls, so applications can configure
translucent menu tint and explicit popup backdrop blur declaratively, including
nested submenus.

## 1.6.5 build 208 - SVG main-view renderer routing

### SVG

`gh_img_view` now keeps ordinary SVG charts on the D2D direct-render path even
when they contain embedded `data:image/*` payloads, preserving vector text,
axes, and line quality in Matplotlib-style exports.

SVGs that actually use image-backed `<pattern>` fills are detected structurally
and routed through the LunaSVG main renderer, avoiding dropped bitmap pattern
backgrounds in assets such as icon-like SVG documents.

Global `* { ... }` CSS rules are now inlined for SVG input, so common
stroke-linecap and stroke-linejoin declarations survive preprocessing before
D2D receives the document.

## 1.6.5 build 207 - host-owned trace sink

### Trace

core-ui no longer writes `GV_BORDERLESS_TRACE` JSONL files by itself. Hosts can
register `ui_trace_set_sink` and receive typed `UiTraceEvent` callbacks for
window and `gh_img_view` trace points. Output policy now belongs to the host:
enablement, filtering, persistence, and analysis are all caller-owned.

Existing borderless/paint/viewport trace points now emit structured fields via
`UiTraceField` instead of preformatted JSON snippets.

## 1.6.5 build 206 - title-bar drag intercept callback (L219 follow-up)

### New API — `ui_window_on_titlebar_drag` / `ui_window_set_titlebar_drag_intercept`

A host can now intercept *dragging the title-bar background* instead of letting
it start the system window-move loop. Use case: a fullscreen window whose
title-bar overlay should, when dragged, restore the pre-fullscreen window.

- `ui_window_set_titlebar_drag_intercept(win, on)` — when on, a left-press that
  hits the `TitleBar` background (not a caption button — those hit-test to
  themselves) does **not** enter `DefWindowProc`'s move loop. Instead the window
  captures the mouse and tracks movement; once it passes the system drag
  threshold (`SM_CXDRAG`/`SM_CYDRAG`) it fires `onTitleBarDrag`. A press that
  releases before the threshold is treated as a click and fires nothing.
- `ui_window_on_titlebar_drag(win, cb, userdata)` — the callback invoked on a
  threshold-crossing title-bar drag while intercept is on. The host typically
  restores/resizes the window in it (e.g. leave fullscreen).

After the callback returns, the window is **repositioned under the cursor** (the
grab point keeps its relative spot on the now-restored title bar) and a system
move loop is started, so the window **follows the cursor until release** — the
same feel as dragging a maximized window's title bar to restore it. The host's
intercept flag is expected to be off by the time the callback returns (it just
left the intercepting state), so the re-issued caption press runs the normal
move loop rather than being intercepted again.

When intercept is off (default), title-bar dragging keeps its normal
move-the-window behaviour, so this is opt-in and non-breaking.

## 1.6.5 build 205 - hit-test position:absolute children first (L219 follow-up)

### Hit testing

Build 204 made `DrawTree` paint `position:absolute` children on top, but
`Widget::HitTest` still walked children in plain reverse-DOM order, so an
absolute overlay declared early in the markup was hit-tested *after* later
non-absolute siblings. A top title-bar overlay painted above the canvas was
therefore visible but not clickable — pointer events landed on the canvas
beneath it. `HitTest` now tests `positionAbsolute` children first (reverse DOM
order), then non-absolute children, mirroring the paint order so the top-most
painted element is also the one that receives the click.

## 1.6.5 build 204 - paint position:absolute children on top of siblings (L219)

### Widget paint order

`Widget::DrawTree` painted children in DOM order, so `position:absolute`
children were composited inline rather than above their non-positioned
siblings. An absolute overlay declared early in the markup (e.g. a top title-bar
overlay that appears before the main canvas/body) was therefore painted first
and then covered by the later canvas — only late-in-DOM overlays (e.g. a bottom
toolbar) showed on top.

`DrawTree` now paints non-absolute children first (DOM order), then all
`positionAbsolute` children on top (each in DOM order). This matches the CSS
default that positioned elements stack above non-positioned ones. core-ui does
not parse `z-index`, so ordering among absolute siblings stays DOM-stable. Other
overlays (toolbar, nav/zoom overlays, minimap) were already late in the DOM and
are unaffected.

## 1.6.5 build 203 - center box-shadow blur on the edge, no offset banding (L218)

### Box shadow

`DrawBoxShadow`'s layered blur approximation faded only **outward** from the
shape edge, leaving the interior at flat-max alpha. A shadow with a noticeable
offset (e.g. `box-shadow: 0 -8px 24px`) pushed that flat-max interior band past
the widget edge, so it read as two layers: a solid-ish band plus the soft outer
fade. CSS blur is a Gaussian centred on the shape edge (~50% there, fading both
ways), which renders as one smooth shadow.

The blur layers now span `[-blur/2, +blur]` instead of `[0, +blur]`, so the
shadow also fades inward and the offset band recedes under the widget edge —
matching CSS. Outward extent is unchanged (still `+blur`), so existing shadows
do not shrink. Layer count is raised 6 → 12 for a finer gradient. Inset layers
that would invert (small widget + large blur) are skipped.

## 1.6.5 build 202 - present-only after batched SetWindowPos (L217 follow-up)

### Window resize

Tightens the build 201 lock-frame. In the batched resize path, the new-size
content is already rendered into the back buffer by the prepared (skipPresent)
paint before `DwmFlush`. After `SetWindowPos`, `SetWindowRect` now flips that
back buffer directly via the new `Renderer::PresentBackBuffer()` instead of
re-running a full `OnPaint` + present. This removes the redundant redraw between
`SetWindowPos` and the present, shrinking the geometry-change-to-present gap from
~1-2 ms to microseconds so DWM is even less likely to sample a half-applied
frame. Borderless wheel-zoom feels tighter / more locked to the window.

## 1.6.5 build 201 - DwmFlush lock-frame for batched window resize (L217)

### Window resize

Borderless wheel-zoom resizes the top-level window programmatically on each step.
Even with the prepared-resize machinery (builds 199-200), a programmatic
`SetWindowPos` (HWND geometry, composited by DWM) and the swapchain `Present`
(content) are two events DWM can composite on different frames, producing a
one-frame mismatch — the image briefly lags the window (black gap) or leads it
(clipped/overflow), worse the faster you zoom. Interactive edge-drag resize does
not show this because Windows' modal resize loop gives DWM atomic resize handling
that a programmatic `SetWindowPos` cannot get.

`SetWindowRect`, inside a visual update batch, now renders the new-size content
into the back buffer (prepared paint), then calls `DwmFlush()` to block until the
next DWM composition boundary before `SetWindowPos` + the immediate present. This
lands the window geometry change and the new content inside the same composition
interval, so DWM picks up both together on the next frame, eliminating the
one-frame resize mismatch. Only the batched (visual-update) resize path is
affected; ordinary edge-drag / move paths are unchanged.

## 1.6.5 build 200 - notify app during prepared visual resize (L217 follow-up)

### Window visual updates

Build 199 prepared the target render size before a batched `ui_window_set_rect`, but it did not notify the application resize callback before the non-presented prepaint. Applications that resize child canvas widgets from the window resize callback could therefore prepaint with the old child widget size centered inside the new root size, producing a wrong intermediate frame.

`PrepareVisualResize` now dispatches the resize callback immediately after resizing the render target and laying out the root at the target size. If the later real `WM_SIZE` matches the prepared size, core-ui skips the duplicate callback. This keeps the prepared frame aligned with application-managed child sizes while preserving live user edge resize behavior.

## 1.6.5 build 199 - prepared visual resize for batched window geometry (L217 follow-up)

### Window visual updates

`ui_window_set_rect` now prepares the target render size before committing a batched programmatic HWND geometry change. Inside a visual update batch, core-ui resizes the swapchain/back buffer to the target physical size, lays out the widget tree at that target size, and draws one non-presented frame before `SetWindowPos`.

When the resulting `WM_SIZE` matches that prepared size, core-ui skips the duplicate `ResizeBuffers` / layout pass and only dispatches the resize callback. After `SetWindowPos` returns, it immediately presents a frame that already matches both the target window rectangle and the target widget state. This is intended for transactions such as borderless wheel zoom where image zoom and window rectangle change together.

User-driven live edge resize is unchanged and still updates on every `WM_SIZE`.

## 1.6.5 build 198 - restore live canvas edge resize (L217 follow-up)

### Window resize

Rolled back the canvas live-resize deferral introduced in build 196. Mouse edge resizing in canvas/borderless mode must remain live: every `WM_SIZE` goes through the normal immediate `OnResize` and skip-vsync paint path instead of waiting for `WM_EXITSIZEMOVE`.

The view-update batching and `GhImgViewWidget::OnDraw` viewport coalescing from later L217 fixes are kept. This build only removes the interaction-level delay from user-driven window edge resizing.

## 1.6.5 build 197 - defer image viewport during view batch (L217 follow-up)

### Image view

`GhImgViewWidget::OnDraw` now respects active view update batches when it detects a widget rect change. Instead of calling `NotifyViewport` immediately from inside draw, it marks the viewport notification pending and lets `EndViewUpdate` deliver the single coalesced callback.

This removes duplicate viewport callbacks and redundant repaint pressure seen during borderless window zoom transactions where zoom/pan and window geometry are updated in one visual operation.

## 1.6.5 build 196 - defer canvas live resize (L217 follow-up)

### Window resize

Canvas mode now defers expensive live-resize commits while the user is dragging a window edge. During `WM_SIZING` / `WM_SIZE`, core-ui records the last physical size and suppresses per-tick layout, viewport notification, and repaint; on `WM_EXITSIZEMOVE`, it commits one final `OnResize` and immediate paint.

This targets borderless image-viewer resizing where DWM could show a transient stretched old back buffer before the image widget finished recomputing fit/tiles. Normal non-canvas windows keep the original immediate resize/reflow behavior.

## 1.6.5 build 195 - rollback borderless prepaint (L217 follow-up)

### Window visual updates

Rolled back build 194's `SetWindowPos` prepaint inside visual update batches. The L217 trace from the follow-up test showed the prepaint rendered the new zoom into the old window size before the real HWND geometry changed, and that extra frame could take 16-32ms, making the borderless zoom jump more visible instead of less visible.

The build 194 `MulDiv` DIP-to-physical-pixel conversion fix is kept, so centered borderless rectangles still use the same pixel rounding as downstream callers.

## 1.6.5 build 194 - borderless geometry prepaint (L217 follow-up)

### Window visual updates

`SetWindowRect` now uses Win32 `MulDiv` for DIP-to-physical-pixel conversion, matching callers that compute centered borderless rectangles with the same DPI math and removing deterministic 1px width/height drift.

When a real HWND geometry change is submitted inside `ui_window_begin_visual_update` / `ui_window_end_visual_update`, core-ui now pre-paints the already-updated visual state before `SetWindowPos`, then keeps the immediate post-geometry paint from build 192. This reduces the DWM window-change-before-image-change gap that showed up in the L217 trace during fast borderless zoom.

## 1.6.5 build 193 - borderless zoom timing trace (L217 diagnostic)

### Diagnostics

Added a temporary `GV_BORDERLESS_TRACE=1` timing trace used by GuoheView debug builds. The trace writes window geometry, paint/resize, and `ui_gh_img_view` zoom/pan/viewport/draw events into `logs/borderless_zoom_trace.jsonl` so borderless zoom jitter can be diagnosed from a single timestamped timeline.

## 1.6.5 build 192 - immediate paint after batched window geometry (L217 follow-up)

### Window visual updates

When `SetWindowRect` commits real HWND geometry inside a visual update batch, core-ui now immediately paints and validates the current state with `skipVSync` instead of waiting for the outer batch end. This preserves normal `SetWindowPos` / viewport ordering while preventing DWM from temporarily compositing an old back buffer into the new window rectangle during resize-like interactions.

## 1.6.5 build 191 - rollback pending HWND geometry batching (L217 follow-up)

### Window visual updates

Rolled back build 190's deferred HWND geometry commit inside `ui_window_begin_visual_update` / `ui_window_end_visual_update`. Deferring the actual `SetWindowPos` until the end of the batch regressed borderless image zoom behavior in downstream applications, so window geometry changes once again take effect immediately while resize/paint invalidations remain coalesced.

## 1.6.5 build 190 - visual update window geometry batching (L217)

### Window visual updates

Fixed a transient frame issue when `ui_window_set_rect`, `ui_window_set_size`, or `ui_window_set_position` is called inside `ui_window_begin_visual_update` / `ui_window_end_visual_update`. Window geometry changes are now delayed until the outermost visual batch ends, then committed once with the final paint, preventing DWM from briefly compositing an old frame into the new window rectangle during resize-style interactions.

The DIP-to-physical-pixel conversion used by `SetWindowRect` now uses `MulDiv`, matching the public DPI API guidance and avoiding truncation drift against callers that already use rounded DPI conversion.

## 1.6.5 build 189 - uix hit-transparent attribute (L216)

### Uix hit testing

Added static `hitTransparent` / `hit-transparent` attribute support to the `.uix` page widget factory, matching the existing markup builder behavior. Overlays can now render visible content without taking mouse hit-test ownership, so wheel and pointer events can fall through to widgets underneath when every overlay node in that subtree is marked transparent.

## 1.6.5 build 188 - runtime resizable state (L203)

### Window hit testing

Added `ui_window_set_resizable(win, int)` so applications can change a window's resize capability after creation. The setter updates both the internal `resizable_` flag used by `WM_NCHITTEST` and the HWND `WS_THICKFRAME` / `WS_MAXIMIZEBOX` style, preventing borderless/fullscreen windows from returning resize hit-test codes after resize chrome has been disabled.

## 1.6.5 build 187 — 动态 class 移除 absolute 定位残留修复 (L202)

### Reactive layout

修复动态 `:class` 切换移除 `position:absolute` 后，widget 仍残留旧 absolute/side 定位字段的问题。`recomputeStyle` 现在会先恢复编译时定位初始值，再应用当前 CSS；这样基础静态定位仍保留，移除动态定位 class 后会回到正常流式布局。无 ABI 变化。

## 1.6.5 build 186

### Context menu hover performance

右键菜单 popup 之前在每个 `WM_MOUSEMOVE` 都整窗失效并重绘，即使鼠标仍停在同一个菜单项内部；同时每次绘制都会对所有 menu item 的 `customContent` 重新 `DoLayout()`，连续上下滑动时容易造成 hover 跟手性下降。

修复：菜单显示或结构变化时缓存每行矩形并完成一次 item widget 布局；绘制阶段只读取缓存并画 hover/内容/快捷键/子菜单箭头。popup 鼠标移动只在 hover 项或打开的 submenu 变化时才 `InvalidateRect`，鼠标在同一行内部移动不再排队重绘。无 ABI 变化。

## 1.6.5 build 185 — 小 SVG 首帧最小栅格尺寸修复 (L201)

`favicon.svg` 这类 16/32/64px 小 SVG 在初次打开时会按自身很小的 intrinsic 尺寸栅格化，随后被窗口显示流程放大或下采样，容易先看到锯齿；用户缩放一次后触发视口重栅，画面又恢复清晰。

修复：SVG 主视图计算视口 raster 请求尺寸时，如果 SVG 原始最长边小于 512px，则把目标 raster scale 至少抬到 `512 / max(width, height)`，也就是按 512px 长边先高采样，再按当前显示尺寸缩小绘制。大 SVG 和高倍缩放仍沿用原来的视口重栅逻辑。无 ABI 变化。

## 1.6.5 build 184 — draw.io foreignObject fallback 修复 (L200)

draw.io 导出的 SVG 会把 HTML 标签文字放在 `<switch><foreignObject>...</foreignObject><image .../></switch>` 里，实际定位保存在嵌套 XHTML/CSS 的 `margin-left` / `padding-top` 等属性中，同时提供 PNG fallback。build 183 的软件端文字路径化会把这类 `foreignObject width="100%" height="100%"` 当成普通文本块处理，导致文字丢失或全部堆到左上角。

修复：`SvgInlineTextAsPaths` 遇到百分比尺寸的 `foreignObject`，或位于 `<switch>` 且后面有 `<image>` fallback 的 `foreignObject` 时跳过路径化，保留原始结构，让 LunaSVG 继续渲染 draw.io 自带 fallback 图片。普通 `<text>`、`<tspan>` 以及 L199 的 WOFF2 内嵌字体路径化不受影响。无 ABI 变化。

## 1.6.5 build 183 — SVG WOFF2 内嵌字体子集渲染修复 (L199)

build 182 只修了同名 `@font-face` 子集的 family fallback 顺序，但仍没有真正加载 SVG 内嵌 WOFF2：
CSS `src: url(data:font/woff2;base64,...)` 中的分号会被声明解析误截断；即使拿到完整 WOFF2，
DirectWrite `CreateFontFace` 也不能直接把 WOFF2 data URL 当普通字体使用。因此 Excalidraw/Xiaolai
类 SVG 仍可能回退到系统 CJK 字体，和浏览器字形不一致。

修复：CSS 声明解析改为忽略 `url(...)` / 引号内部的分号；通过 `IDWriteFactory5::UnpackFontFile`
先把 WOFF2/WOFF 容器解包为 sfnt 字体数据，再注册到 in-memory font loader。对普通 upright 文本，
按原 SVG `font-family` 顺序逐字符查询每个内嵌子集的 glyph，直接使用命中的 `IDWriteFontFace`
输出 SVG path 轮廓；复杂换行、surrogate、非普通字重/斜体继续走原 DWrite layout fallback。
验证 `不显示文字.svg` 中 `Excalifont + Xiaolai` 多子集字体与浏览器截图一致。无 ABI 变化。

## 1.6.5 build 182 — SVG 内嵌字体子集 fallback 修复 (L198)

部分 Excalidraw SVG 会把 `font-family="Excalifont, Xiaolai, ..."` 搭配多个同名 `@font-face`
WOFF2 子集使用：第一个字体或第一个同名子集不含中文 glyph 时，浏览器会继续落到后续子集，
但 build 181 的 DirectWrite 临时字体集合把同名子集合并/作为单一 fallback 目标，导致文字仍可能回退到系统 CJK 字体。

修复：导入 SVG 内嵌字体时为每个 `@font-face` 子集生成内部唯一 family alias，同时保留原 CSS family 到 alias
列表的映射；创建文本格式时使用首个 alias，fallback 映射时跳过这个首个 alias 并按原 `font-family`
顺序展开其余 alias，再接系统 fallback。这样同名 WOFF2 子集可以像浏览器一样逐字形回退，
`不显示文字.svg` 的 Xiaolai/Excalifont 字形在主视图中与浏览器渲染一致。无 ABI 变化。

## 1.6.5 build 181 — SVG 内嵌字体路径化渲染 (L197)

含内嵌 `@font-face` / `data:font/woff2;base64,...` 的 SVG 在浏览器里能显示原字体,
但 LunaSVG/PlutoVG 只支持 stb_truetype 可读的字体数据, 也没有浏览器式逐字形 fallback;
因此主视图虽然已能清晰显示文字, 字体仍可能 fallback 到系统 CJK 字体, 与浏览器不一致。
修复: SVG 主视图加载时先通过 `Renderer::SvgInlineTextAsPaths` 处理 `<text>/<foreignObject>`:
解析 SVG 内嵌 `@font-face`, 用 DirectWrite in-memory font loader 建临时字体集合,
保留 CSS `font-family` 列表并构造 DWrite fallback, 将文字按内嵌字体/回退结果转成 `<path>` 字形轮廓,
再交给 LunaSVG 继续按视口动态重栅。这样 Excalidraw/Xiaolai 等 WOFF2 子集字体在主视图中
尽量贴近浏览器显示, 且放大清晰度继续沿用 L196 的视口重栅策略。无 ABI 变化。

版本号规则：`MAJOR.MINOR.PATCH.BUILD`

- `MAJOR/MINOR/PATCH`：语义化版本，对应 `CMakeLists.txt` 的 `UI_CORE_VERSION`
- `BUILD`：构建编号，对应 `CMakeLists.txt` 的 `UI_CORE_INTERNAL_VERSION`
  - 每次发布构建 +1；同一 MAJOR.MINOR.PATCH 下可累加
- DLL 属性里看到的 `FileVersion` 即该四段串，由 `src/version.rc.in` 注入

## 1.6.5 build 180 — SVG 主视图高倍放大文字清晰度修复 (L196)

主交互视图的 SVG 虽已由 core-ui 端持有 LunaSVG document, 但渲染策略仍是把整张 SVG
栅格化成单张缓存位图再 `DrawBitmap`。该整图缓存有固定尺寸上限, 高倍放大含 `<text>`
的 SVG 时会从旧位图上采样, 文字边缘变软。

修复: `gh_img_view` 的 SVG 模式改为按当前可见视口栅格化。绘制时用当前
`zoom/pan/rotation` 反算 widget 覆盖的 SVG 源坐标矩形, 加 overscan 后只渲这块区域;
缓存记录源坐标 + 输出像素密度, 平移在覆盖范围内直接复用, 缩放换档则后台渲最新视口块。
首帧无缓存时同步渲一块避免空白。主视图保留 LunaSVG 的 CSS/text/filter 兼容性, 但不再
受整图 4096px 档位限制。无 ABI 变化。

## 1.6.5 build 179 — SVG <text> 中文不渲染修复 (L195)

含 `<text>` 的 SVG (尤其中文) 文字渲染不出/豆腐。根因: LunaSVG 选定单一字体、无逐
字形回退, 其 `generic_fallbacks` 把 sans-serif→Arial / serif→Times New Roman /
monospace→Courier New 硬编码成纯拉丁字体; SVG 文字 font-family 列表里自定义/
`@font-face` 字体 LunaSVG 都不认 (不解析 @font-face), fallback 到 sans-serif→Arial
→ 中文字形落空成豆腐。

修复: `SetSvgFromFile` 加载前 lazy 注册一次系统 CJK 字体 (微软雅黑/等线/黑体/宋体,
GetWindowsDirectory+Fonts 定位, 读一次共享 buffer), 注册到 generic fallback 目标名
(Arial/Times New Roman/Courier New) + empty family + bold/italic 4 档。FontFaceCache
新条目插链表头 → 覆盖系统同名拉丁字体 → SVG 文字 fallback 时改用 CJK 字体 → 中英文都
渲染。无 ABI 变化。

## 1.6.5 build 178 — 超长/超宽图缩放底部内容溢出修复 (L194)

`LevelToScreenScale` 只返回宽度比 (fullW/LevelW), 却被当单一 scale 同时缩放 X 和 Y
(`DrawLevel` 瓦块位置 + `NotifyViewport` 可见范围)。`LevelWidth`/`LevelHeight` 各自
独立 floor 折半, 普通宽高比无碍; 但极端宽高比 (如 1080×29679 超长图, 27.5:1) 在
coarse level 宽比≠高比 (lvl5: 32.73 vs 32.02, lvl6: 67.5 vs 64.1), 用宽比缩 Y 让
该级在屏上比图像真高多出 659~1573 px(×zoom), 内容顶部对齐 destTop、多余高度从底边
溢出图像下方 → 缩放到某档底部出现暗块 + 残影。

修复: 新增 `LevelToScreenScaleY` (fullH/LevelH); `DrawLevel`/`NotifyViewport` 的
scale 拆 `scaleX`(宽)/`scaleY`(高), 所有 Y 坐标用 scaleY、X 用 scaleX。每级各轴
精确填满 dest, 无溢出。普通图 level 0-3 误差 0 不受影响; 旋转兼容。

## 1.6.5 build 177 — PickInterp 大幅缩小档恢复 HQ_CUBIC (L191)

L190 把 `<1.0` 全档改 LINEAR 过头, 顺带把大幅缩小 `<0.5` 也锁死 LINEAR。
LINEAR 2×2 双线性在大幅下采 (如 13.8MP 文档扫描单层图 fit 到 ~0.3x) 漏采
~70% 源像素 → 文字锯齿发硬, 跟系统看图器 (真·区域降采) 差距明显。

`PickInterp` 改三档:

- `<0.5` 大幅缩小 → **HQ_CUBIC**: D2D 对大幅下采带 prefilter 抗欠采样; 源边缘已被
  prefilter 抹成渐变, cubic 负权【不会 ring】(初版"大幅下采 cubic 会 ring"是想当然)。
- `0.5~1.0` 适度缩小 → LINEAR: 边缘仍锐, 防 HQ_CUBIC 过冲"锐化过头" (L190 不变)。
- `>=1.0` 放大 → HQ_CUBIC: 上采平滑。

金字塔大图 (活动层落 0.5~1.0) 不受影响, 仍走 LINEAR (无退化)。

## 1.6.5 build 176 — gh_img_view 文字过冲 + 放大缩回锯齿两修复 (L190)

GuoheView 看大尺寸文字图暴露的两个独立渲染缺陷, 均在渲染层, 与 BC7 / 金字塔无关。

### 1. PickInterp 阈值 0.5→1.0 — 消除文字"锐化过头"

HQ_CUBIC 负权在 `0.5~1.0` 缩小档对高对比文字/线稿边缘**过冲 ring 出光晕**, 用户感知
为"锐化过头"。该档 (fit 看图主场景: 活动层略缩小) 改用 LINEAR 双线性无过冲。仅放大
(≥1) 保留 HQ_CUBIC 取其上采平滑。

### 2. OnDraw 只画 `lvl >= active` — 消除放大缩回后的锯齿/糊

放大时加载的高清层 (如 L0) 作 `prevActiveLevel` 保留, 缩回 fit 时被 `DrawLevel` 从小到
大画在**最上层** → 当前缩放下大幅缩小 (15x) ring/锯齿, 盖住正确的活动层 (L3), 表现为
"放大→缩回后文字发硬、部分区域看着坏了"。改为只画 **活动层 + 更低清层** (画其下方兜底
gap), **不画更高清的残留层**。

## 1.6.5 build 175 — msgbox 默认不显焦点环 (首次方向键/Tab 才现身)

build 174 让 msgbox 开框即把焦点设到 `default_idx` 按钮 + 亮焦点环。但鼠标用户不该
一开框就看到焦点环 —— 改为标准行为: **默认无焦点, 按方向键/Tab 才让焦点现身**。

- 去掉显示后的初始 `ui_window_focus_widget` 调用 → 开框 `focusedBtnIdx=-1`, 无焦点环。
  回车无焦点时仍走 OnKey 兜底 = `default_idx` (调用方设"取消"=安全 → 默认回车=取消)。
- 每按钮注册 `ui_widget_on_focus` 同步 `focusedBtnIdx` (覆盖 Tab/方向键/点击各路径)。
- 方向键: 首次焦点现身于 `default_idx` (不移动), 之后 ±1; Tab 走内置 FocusNext + 亮环。
- `default_idx` 按钮 accent 主样式保留 (标示默认动作, 与焦点环无关)。
- `dialog_state.focused_idx` 开框为 -1, 导航后同步。ABI 无变化。

## 1.6.5 build 174 — msgbox 按钮键盘焦点导航 (方向键 + 回车确认聚焦按钮)

### 需求
确认弹窗要支持"方向键选按钮 + 回车确认, 默认回车落在取消上"。此前 msgbox 显示后
焦点在窗口(非按钮), 回车固定触发 `default_idx`; 方向键不移按钮焦点。

### 实现
利用已有内置: `DispatchKeyDown` 里"聚焦 widget 按 Enter/Space → onClick"早已存在,
故只需把初始焦点落到按钮 + 让方向键移按钮焦点, Enter 自然跟随焦点触发。
- 新 C API `ui_window_focus_widget(win, widget)`: 编程式设键盘焦点 + 亮焦点环 (视同
  键盘导航)。包 `UiWindowImpl::FocusWidget` (= `SetFocus` + `showFocusRing` + `Invalidate`)。
- `msgbox`: 显示后初始焦点落 `default_idx` 按钮 (调用方设成"取消"=安全默认 → 默认回车
  =取消); `OnKey` 加 ←/→ 在按钮间循环移焦点 (Esc→cancel 保留; Enter 兜底跟随焦点)。
- `dialog_state` IPC 加 `focused_idx` 字段 (验证焦点移动)。

### 影响
ABI 纯增量 (新增 1 个 C API + UiWindowImpl public 方法)。任意 `ui_msgbox_ex` 对话框
自动获得方向键导航 + 回车确认聚焦按钮; 初始焦点 = `default_idx`。

## 1.6.5 build 173 — msgbox IPC 自动化 + 每按钮快捷键绑定

### 问题
下游用 IPC 自动化测"确认弹窗"流程时, `ui_msgbox` 模态没法脚本驱动 —— `dialog_confirm`
/`dialog_cancel` 只在 debug server 的 help 字符串里列着、根本没实现 handler (返回
unknown)。且按钮快捷键只有固定的 Enter(default_idx)/Esc(cancel_idx)。

### msgbox IPC 自动化 (ui_debug_server)
- `dialog_state` → 当前活动 msgbox 快照: `{active,title,buttons[],default_idx,cancel_idx,
  button_count}`, 供断言。
- `dialog_confirm` → 触发 `default_idx` 按钮 (= Enter 绑定)。
- `dialog_cancel` → 触发 `cancel_idx` 按钮 (不可取消的框返错)。
- `dialog_click <idx>` → 触发指定按钮。
- 机制: `DispatchOnce` 整体经 `ui_window_invoke_sync` 在 UI 线程跑, 模态的
  `GetMessage(NULL)` loop 会 pump 该 invoke, 故命令直接调 `MsgBoxDebugSnapshot/Finish`
  (同线程访问 `g_activeBox`, 无锁)。msgbox 进模态注册 `g_activeBox` (嵌套 save/restore)。

### 每按钮快捷键绑定
- `UiMsgBoxParams` 末尾 append `const int* button_keys` (每按钮 VK 键码, 0=无;
  struct_size 护栏向后兼容)。`OnKey` 命中 `button_keys` 即触发该按钮, 优先于
  `default_idx`(Enter)/`cancel_idx`(Esc) 兜底。供"是=Y/否=N"类多按钮框。
  ("Enter 绑定确定/取消"原本就能用 `default_idx` 表达, 本字段是更通用的每按钮任意键。)

### 影响
ABI 纯增量 (UiMsgBoxParams 末尾 append + struct_size 护栏), 旧调用方零改。
下游可脚本化测模态确认流程 (如 GuoheView"保存旋转覆盖"的覆盖确认框)。

## 1.6.5 — SVG 渲染升级 / 启动·拖动性能 / 独立 msgbox / 反应式菜单

> 整合自 1.6.0 公开发布以来的能力扩展与实战 bug 修复（build 135 → 172）。完整 per-build 历史见下方各 build 段。

### 🎨 SVG 渲染
- `<text>` z 序、多 `<tspan>`、`font-style` / `font-weight` / 渐变 fill 修复
- 物理单位 `width`/`height`（mm/cm/in）覆写 viewBox，修内容缩成一小块
- 裸 `<use href>` + `<symbol>` 渲染

### 🚀 启动 / 性能
- `start_maximized` 真最大化 + 隐藏，首帧即全屏
- 共享 D3D11 设备后台预热，首窗创建提速
- 隐藏窗 Present 死锁修复 + 拖动窗口性能（删 `DwmFlush` / `WS_EX_COMPOSITED`）

### 🪟 窗口 / 菜单 / 控件
- 独立模态询问框 `ui_msgbox` / `ui_msgbox_ex`（替代 in-window `ui_dialog_*`）
- Menu 反应式重构：`<menu>` Vue 3 响应式渲染、submenu 父行支持图标、副屏定位修复
- 任意 widget 事件回调：自绘 `<custom>` 也能接 `on_mouse_*` / `on_focus` / `on_wheel`
- toast 独立合成叠加窗，淡入淡出不卡顿
- 其它：HBox `flex-shrink`、`set_visible` 立即生效、IME `VK_PROCESSKEY`、`on_resize` 单位统一 DIP

### 💥 BREAKING（从 1.6.0 升级）
- `ui_dialog_*` 移除 → 改用 `ui_msgbox`
- `ui_toast_ex` 新增 `anim` 参数；`ui_toast_at` 移除
- menuitem 改 widget 模板：`<menuitem>X</menuitem>` → `<menuitem><label>X</label></menuitem>`

## 1.6.0 build 171 — lunasvg 支持 light-dark()/var() + `<switch>` 降级渲染 (L183/L184)

### 问题

`ui_gh_img_view` 的 SVG 引擎(L173/Phase 4 由 D2D 换成 LunaSVG)渲染 drawio 28+ 导出的
SVG(test.drawio.svg 看板)时**整图黑底 + 卡片文字全丢**, 与参考输出(白底+黑框+斜线
填充+文字)完全不符。根因是 LunaSVG 两处能力缺口:

### L183 — CSS Color-4 `light-dark()` / `var()` 不识别 → 退化成黑底

drawio 全幅背景 `<rect fill="#ffffff" style="fill: light-dark(#ffffff, var(--ge-dark-color,#121212))">`,
inline style 按 CSS 级联覆盖 presentation 属性的 `#ffffff`。plutovg 不认 `light-dark()`
→ `parseColorValue` 返 `nullopt` → 落 `fill` 默认黑(整图黑底)。同款 `light-dark()`
还遍布 stroke/text(浅深反转)。

修复(`svglayoutstate.cpp`): `parseColorValue` 进 plutovg 前先拦截 —— `light-dark(A,B)`
取浅色第一参 A(我们按 light-mode 栅格化), `var(--x,fb)` 取兜底 fb; `matchColorFunction`
按括号深度 + 顶层逗号切参(不误切 `rgb()`/`hsl()` 内层逗号)。颜色层通用增强,
fill/stroke/color/stop-color 全受益。

### L184 — `<switch>` 整树被解析器跳过 → `<image>` 降级图全丢

`switch` / `foreignObject` 均映射 `ElementID::Unknown`, svgparser 命中即 `ignoring`
整棵子树 → 连作为降级回退的 `<image>` PNG 一并丢弃。drawio 文字全靠它。

修复(`svgelement.{h,cpp}`): 注册 `<switch>` 为已知元素 + 新增 `SVGSwitchElement`, 按
浏览器语义只渲染第一个可显示子元素; `foreignObject` 仍是 Unknown 在解析期出局,
自然落到 `<image>` 分支。完整 requiredFeatures/systemLanguage 求值未实现(简化为首个
可显示子元素), 对 drawio/mxgraph/Visio 的 "foreignObject + image 降级" 正确。

### 影响

drawio/mxgraph/Visio 导出的 SVG 在看图器主视图正确渲染。无 ABI 变化, 下游换 dll 即吃。
与 `gh-img-decode` 仓 944f1a4 是同一补丁(那份修 ghde 解码/缩略图路, 本份修主视图渲染;
两份 vendored lunasvg 副本独立, 必须同步改)。

## 1.6.0 build 170 — toast 改独立合成叠加窗, 消除淡入淡出卡顿 (L182)

### 问题

toast 通知原本画进**主窗 D2D RT**(`DrawToast` 在 OnDraw 末尾), 淡变靠主窗 `WM_TIMER`
(16ms) `Invalidate()` 驱动。两个卡顿源:
- WM_TIMER 是最低优先级消息 + 系统默认 15.6ms 计时粒度(无 `timeBeginPeriod`)→ tick
  不准不匀;
- 每个淡变帧都**重渲整个主窗**(含大图)→ 大图下单帧 >16ms 直接丢帧。
时间基进度 catch-up 只保证"进度对", 不保证"帧数足且匀" → 视觉一跳一跳。

### 改动(内部重构, 对外 `ui_toast_ex` 签名/行为不变)

toast 改成**独立的 DirectComposition 透明叠加窗**(照 `ContextMenu` 弹窗那套
`CreateRenderTargetForLayered`):
- `ShowToast` 量文字算 box 尺寸 → 相对主窗客户区定位(含 DPI)→ 建
  `WS_POPUP | WS_EX_LAYERED 等 + WS_EX_TRANSPARENT`(点击穿透) 叠加窗, owner=主窗;
  专属 composition-mode `toastRenderer_`。
- 淡变只 `PaintToast` 重渲那个**小 toast 窗**(box 画在 (0,0)), 完全不碰主窗 RT →
  大图也丝滑。FADE 改 alpha; SLIDE 用 `SetWindowPos` 移窗。
- 动画期间 `timeBeginPeriod(1)` 把计时精度提到 1ms 让 16ms timer 准点 ~60fps,
  结束 `timeEndPeriod(1)`(严格配对, `DestroyToast` 收尾)。
- timer 移到 toast 窗的 `ToastWndProc`; 删主窗 OnDraw 的 `DrawToast` 调用 + 主窗
  WndProc 的 toast timer 段; 主窗销毁/析构时 `DestroyToast` 兜底清理。
- 链接 `winmm`(`#pragma comment`)+ `<timeapi.h>`。

## 1.6.0 build 169 — pan 开关升级为按轴锁 set_pan_lock (L178 修正)

### `set_pan_enabled(on)` → `set_pan_lock(lock_x, lock_y)`（破坏性替换）

build 168 的 `ui_gh_img_view_set_pan_enabled`(全开/全禁) 表达力不够：下游(GuoheView
长图"锁定")真实需求是**锁水平、留垂直**(左右固定居中、上下仍可拖动阅读)，而非全禁。
按"lib 早期破坏性变动允许、不留双轨"原则**直接替换**(build 168 仅 GuoheView 一个 caller)：

- 删 `ui_gh_img_view_set_pan_enabled` / `get_pan_enabled`
- 加 `ui_gh_img_view_set_pan_lock(w, lock_x, lock_y)` / `get_pan_lock(w, *out_x, *out_y)`
  - `lock_x/lock_y=1` 时该轴拖动不平移（默认两轴 0=自由）
  - 全锁 `(1,1)` 等价旧 `set_pan_enabled(0)`；长图锁水平 `(1,0)` = 新需求
- 实现：`OnMouseMove` 按轴 gate —— `if(!panLockX_)panX_=…; if(!panLockY_)panY_=…;`，
  全锁时 early-return（无平移无重绘）。命令式 `set_pan` 不受影响；`onMouseMoveHook` 照常 fire。

## 1.6.0 build 168 — gh_img_view 加"禁用拖动平移"开关 (L178)

### 新增 `ui_gh_img_view_set_pan_enabled` / `get`（锁定/只读视图）

下游(GuoheView 长图"锁定画布")需要冻结鼠标拖动平移、只保留命令式/滚轮移动，但
gh_img_view 此前只有 `set_wheel_zoom_enabled`(滚轮)、没有拖动平移开关，拖动平移是
widget 内部 `OnMouseMove` 处理，宿主无法 gate。

**新增对称 API**（跟 `set_wheel_zoom_enabled` 一致语义）：
- `ui_gh_img_view_set_pan_enabled(w, on)` / `ui_gh_img_view_get_pan_enabled(w)`
- `on=1`(默认) 拖动平移图片；`on=0` 拖动不 pan（锁定/只读视图）。
- 实现：`OnMouseMove` 在 `dragging_` 复检后、`panX_/panY_` 计算前加 `if (!panEnabled_) return true;`，
  **故意放在 `onMouseMoveHook` 之后** → 宿主"拖出图片"等手势仍能观察 move、只是图本身不动；
  也不触发多余 `InvalidateAllWindows`。
- **命令式 `set_pan` 不受此开关影响**（GuoheView bar 按钮/滚轮走 `set_pan` 在锁定态照常移动）。
- 纯新增 API + 1 个成员 + 1 行门控，不改任何签名/结构 → 无 ABI 破坏。

## 1.6.0 build 167 — 隐藏窗 WM_PAINT 改"绘制但不 present", 修 L175 守卫致的重绘洪流 (L177)

### L175 整帧跳过隐藏窗绘制 → 渐进上屏 caller 的 WM_PAINT 洪流 (L177)

build 166(L175) 给隐藏/最小化窗的 `WM_PAINT` **整帧跳过**(`ValidateRect`+return), 避开 DWM 未合成窗的 swapchain `Present1` 在 AMD 驱动死锁。但"整帧跳过"有副作用: "预创建隐藏窗 + `ui_run` 渐进上屏"的 caller(如 GuoheView 启动 hold 路径打开缓存命中的大图)因 widget **永不绘制** → 被瓦片交付反复 `ui_window_invalidate` → `WM_PAINT` 洪流, 把消息泵塞满 → 超长图(2560×143629)缓存命中**启动卡死**(GuoheView bug-075)。

**修复**——改为"绘制但不 present":
- `Renderer` 加 `skipPresent` 标志(类比 `skipVSync`): `EndDraw` 照常 `ctx_->EndDraw()` 把 D2D 绘制 flush 到 back buffer, 仅在 `!skipPresent` 时才 `swapChain_->Present1()` flip; 两标志每次 `EndDraw` 复位。
- `WM_PAINT`/`WM_DISPLAYCHANGE` 处理: 隐藏/最小化窗设 `skipPresent` 后**照常 `OnPaint`**(绘制 → widget 状态落定、不再 invalidate, 消除洪流), 只跳 swapChain flip(不撞驱动死锁)。
- `ShowImmediate` 直调 `OnPaint` 不设此标志, 正常上屏路径零影响。

`ctx_->EndDraw()`(D2D flush, 纯 GPU 工作)不会像 swapChain `Present`(DWM flip)那样在隐藏窗死锁, 故安全。实测: GuoheView 超长图缓存命中出窗 1036ms→628ms、出窗后 CPU 空转 0、显示正确。无 ABI 变动。

## 1.6.0 build 166 — 隐藏/最小化窗口 WM_PAINT 跳过 Present, 修隐藏窗 flip present 死锁 (L175)

### 预创建隐藏窗 + ui_run 在某些 GPU 驱动上 Present 死锁 (L175)

GuoheView L175: `GuoheView-Updater.exe --auto` 后台检查无更新时永久僵死(几分钟+)。

**根因**(实时调试栈坐实): 更新器 Auto 模式用 `ui_page_prepare_window` 预创建**隐藏窗口**(带 DXGI flip-model swapchain, 准备有更新再 `show`)。`ui_run` 消息循环收到初始 `WM_PAINT` → `OnPaint` → `EndDraw` 内 `IDXGISwapChain::Present1`。该 Present flip 到一个 **DWM 根本没在合成的隐藏窗口**, 等不到 back buffer 释放, 在 GPU 驱动(实测 AMD `amdxx64`)里**永久阻塞**。主线程卡死在 Present、回不到消息循环 → 后台 worker 检查早完成(无更新)却没人读 → `ui_quit` 永不触发 → 进程僵死。机器相关(取决于 GPU 驱动对隐藏窗 flip present 的处理)。

调试栈: `amdxx64 → d3d11!Present → dxgi!CDXGISwapChain::Present1 → core_ui!UiWindowImpl::OnPaint → HandleMessage(WM_PAINT) → ui_run`。

**修复**: `WM_PAINT`/`WM_DISPLAYCHANGE` 处理处加可见性守卫 —— `!IsWindowVisible(hwnd_) || IsIconic(hwnd_)` 时只 `ValidateRect` 返回, 不走 `OnPaint`→`Present`。隐藏/最小化窗口 DWM 不合成它, present 它既无意义又会卡驱动。

**为何守卫在 WM_PAINT 而非 OnPaint**: `ShowImmediate` 故意在 `ShowWindow` 之前"直调"`OnPaint` 把首帧画进 RT(消除显示瞬间黑屏), 那时窗口尚不可见 —— 若在 `OnPaint` 内挡可见性会把首帧渲染也挡掉。死锁的是消息循环派发的 `WM_PAINT`, 故只在该消息路径加守卫, 不碰 `ShowImmediate` 直调路径; 窗口 `show` 后再来的 `WM_PAINT` 正常绘制。

通用健壮性修复: 任何"预创建隐藏窗 + 稍后 show"(`ui_page_prepare_window`)或最小化态被 `Invalidate` 的 core-ui 应用都受益。无 ABI 变动。

## 1.6.0 build 165 — 拖动窗口性能: 删 DwmFlush / WS_EX_COMPOSITED / 死代码 (L174)

### borderless 窗口连续拖动越拖越掉帧 (L174)

GuoheView L174: borderless 看图窗按住标题栏连续拖动, 拖久掉帧越来越严重。

**根因**:
- `WM_MOVING` 每次同步调 `DwmFlush()`, 阻塞到 DWM 下一次合成(约 1 个 vblank)。系统 modal move loop 因此被锁到 ≤刷新率; 高回报率鼠标的输入比 loop 排空更快 → 窗口位置积压、越拖越落后于光标(输入积压累积, 非资源泄漏)。而窗口移动期间根本不重绘(无 `OnPaint`), DWM 只平移已合成 surface —— `DwmFlush` 当年压的"拖动果冻"来自旧的"边拖边重绘"架构(build `9056bcc` 的 `CreateDragCache` 时代), 重构成"移动不重绘"后该 `DwmFlush` 已失去意义, 纯属负担。
- `WS_EX_COMPOSITED` 自窗口创建起常驻: core-ui 是单 HWND + D2D 自绘、**无子窗口**, 该 flag(给子窗自下而上双缓冲 alpha 用)在此零收益, 却让 DWM 每次合成多走一层重定向双缓冲, 拖动/缩放平添开销。
- `CreateDragCache` / `dragCache*` 成员是 `DwmFlush` 取代旧位图缓存方案后留下的**死代码**(全仓零调用)。

**改动**:
- 删 `WM_MOVING` 的 `DwmFlush()`(整个 case 移除, 回落 `DefWindowProc` 默认处理)。
- 窗口 `exStyle` 去掉 `WS_EX_COMPOSITED`(保留 `WS_EX_LAYERED` 给开场动画/透明路径, 走独立的 `SetLayeredWindowAttributes`, 不受影响)。
- 删 `CreateDragCache`/`ReleaseDragCache` 定义+声明 + `dragCacheBitmap_`/`dragCacheDpi_`/`dragCacheWidth_`/`dragCacheHeight_` 成员 + `WM_SIZING`/`WM_EXITSIZEMOVE` 里的 `ReleaseDragCache` 调用。

`UiWindowImpl` 为 core-ui.dll 内部实现类, 不在公开 C API(`ui_core.h`)暴露, 删 private 成员/成员函数不构成对下游的 ABI 形状变动 —— 仅 build number +1。

## 1.6.0 build 164 — 子菜单父行支持图标/富内容 (L172)

### 反应式 <menu> 子菜单父行可写 <svg>+<label> 富内容, 跟 menuitem 一致 (L172)

GuoheView L172: `<menu :title="...">` 合成的子菜单父行只能显纯文字, 无法像 `<menuitem>` 那样带前置 `<svg>` 图标——子菜单父项 (复制更多 / 打开方式 / 更多) 跟同级带图标的 menuitem 视觉不一致, 菜单图标列空一格。`compiler.cpp` 的 submenu 分支写死合成 `<div class="menuitem-row"><label>{title}</label></div>`, 且对非 menuitem 子元素直接报错。

通用修复: 把 menuitem 的"子节点深拷成 `menuitem-row` wrapper"逻辑 (cloneNode + Text/Interpolation 包 `<label>` + 元素原样克隆) 上提为 `compileMenu` 内共享 helper `buildRowWrapper`, submenu 父行与 menuitem 行共用。submenu 父行用 `buildRowWrapper(skipStructural=true)` 取 `<menu>` 的非结构性子节点 (svg/label/img/...) 当外观; 没写内容子节点时回落到 `:title`/`text` 合成 `<label>` (**老用法不破**)。`<menu>` 体内非 menuitem/menu/separator 元素: top-level 仍报错 (拦笔误), submenu 体内静默跳过 (已被父级当 entry content 消费), 避免递归 compileMenu 重撞报错。

新写法 (跟 `<menuitem>` 完全一致):

```xml
<menu corner-radius="6">
  <svg .../>                       <!-- 父行图标 -->
  <label>@menu.copy_more</label>   <!-- 父行文字 -->
  <menuitem id="...">...</menuitem>
</menu>
```

现有 `<menu :title="$t(...)">` (打开方式/更多) 不受影响。无 ABI 变动 (仅 .uix 编译期 AST 处理), `page_state.cpp` 不用改 (submenu entry 跟 menuitem 走同一 `contentRoot` 实例化路径)。

## 1.6.0 build 163 — SVG 物理单位 width/height 覆写 viewBox 修内容缩放错位 (L170)

### gh_img_view + image_source_svg: SVG root width/height 用物理单位(mm/cm/in)时内容缩成一小块 (L170)

CorelDRAW / Illustrator / Inkscape 导出的 SVG 常用 `width="443.979mm"` + `viewBox="0 0 44397.94 44397.94"`（width 物理单位，viewBox 大数值坐标系）。`SetSvgFromFile`（gh_img_view）/ `CreateSvgSourceFromFile`（image_source_svg）读 viewBox 当 native size 并据此算 scale（`drawW/svgW_`），但**没覆写 root 的 width/height** → D2D `DrawSvgDocument` 按 root 原始 mm 固有尺寸（≈1678px）渲染 SVG 内容，跟 viewBox-based scale 不一致 → 内容缩 ~25 倍（宿主打开几乎看不到，仅左上角一小块）。

修复：读完 viewBox 后覆写 root `width/height = viewBox 数值`（`D2D1_SVG_LENGTH_UNITS_NUMBER`）消除单位歧义，跟 ghde `svg_decoder` 同款覆写。width 本就是无单位/px 的 SVG（icon 类、viewBox==width）行为不变。实测 443.979mm/viewBox 44397 的图：修前缩成 ~48px、修后 fit 满窗口 1242px。ABI 不变。

## 1.6.0 build 162 — gh_img_view Begin 加 keep_preview (L168, 大图切金字塔保留 preview 兜底层消除闪烁)

### gh_img_view: Begin 新增 keep_preview，切金字塔保留 preview 兜底层 (L168)

`UiGhImgViewInfo` 新增 `keep_preview` 字段（占原 `reserved[0]`，**ABI 不变**）：置 1 时 `ui_gh_img_view_begin` 只替换金字塔结构、**不清空现有 preview 兜底层**，让宿主切到金字塔会话时保留已上屏的清晰低清图。

**动机**：宿主打开大图（如 18641² 卫星图）走慢路径，先用 phase0 预览（4096²）秒出清晰图；随后 set_pyramid_image 的 begin 无条件 preview_.Reset()、退回金字塔最小级（291²）现场合成垫底，等可见 tile 落地才恢复清晰 —— 肉眼可见「清晰→模糊→清晰」闪烁。preview 与金字塔 tile 在 OnDraw 本是两层独立绘制，begin 把两者捆绑重置才逼出闪烁。

**行为**：keep_preview=1 时 begin 保留 preview_/previewW_/previewH_，OnDraw 第一层继续兜底，tile 逐级盖上 → 清晰度物理单调。默认 0 = 旧行为（begin 清 preview）。

## 1.6.0 — build 161

### ui_widget_set_layout_pinned — 可拖浮动面板布局豁免

pinned=1 且组件已有有效 rect 时, relayout 不再改写其位置 (子树照常布局,
显式 set_rect 仍有效)。解决"用户拖动定位的 absolute 组件被任何 relayout
打回 CSS 默认位"(GuoheView 鸟瞰图: 拖走后右键弹菜单瞬间跳回)。仅对
position:absolute 组件生效。

## 1.6.0 — build 160

### msgbox 按钮自定义色 + 按钮默认手型

UiMsgBoxParams 新增 `button_colors` (NULL=全默认; 有色按钮=实底+按亮度
自动黑白字+自动 hover/press)。ui_msgbox_ex 的 struct_size 改为分段取
字段 — 旧编译调用方仍合法。Button/IconButton 默认 cursor=pointer
(CSS 显式可覆盖); CSS 自定义底色按钮的文字按底色亮度自动黑白。

## 1.6.0 — build 159

### ui_msgbox_ex — params/result 成对结构 + 勾选框

`UiMsgBoxResult ui_msgbox_ex(win, const UiMsgBoxParams*)` — 入参 struct
(struct_size 护栏) / 返回 struct {button, checked, _reserved[2]}, 两端加
字段都不动签名。新增 verification 勾选框 (check_text/check_initial,
系统 TaskDialog 同位)。ui_msgbox 保留为便捷皮。

## 1.6.0 — build 158

### ui_msgbox — 独立窗口模态询问框 (L148)

`int ui_msgbox(win, title, message, buttons, n, default_idx, cancel_idx, icon)`
— 像系统 MessageBox: 同步阻塞返回点击索引、1..4 个自定义按钮、Enter=默认
(primary accent)、Esc/关闭=取消、四款语义图标。独立 frameless 窗口
(owner=宿主, 窗口形状交系统 DWM — Win11 圆角/Win10 直角), TitleBar 可
拖动, 内容尺寸按文本测量自适应 (宽 320..540 DIP), 模态期间宿主禁用。

### BREAKING: ui_dialog_* 移除

in-window 覆盖层式 `ui_dialog` / `ui_dialog_show` / `ui_dialog_hide` /
`ui_dialog_set_ok_text` / `ui_dialog_set_cancel_text` /
`ui_dialog_set_show_cancel` / `ui_dialog_set_theme_mode` 七个 API 与
DialogWidget、debug IPC 的 `dialog_confirm`/`dialog_cancel` 命令一并移除。
迁移: `ui_dialog_show(ok/cancel)` → `ui_msgbox(两按钮)`。

## 1.6.0 — build 157

### 修复 / 渲染
- **L127 — `RequestLayout()` 现在会催生一帧**：此前它只置全局脏旗标，而
  旗标仅在 `OnPaint` 里被消费——完全空闲的窗口永远等不到那次 paint，导致
  `ui_titlebar_set_title` / `ui_label_set_text` 等"纯数据变更"在无其他
  UI 活动时**永不上屏**（典型症状：慢加载期间仅改标题的提示文字从不显示；
  调试 IPC 的强制渲染会掩护此缺陷，肉眼/截屏才能复现）。现
  `RequestLayout()` = 置旗标 + `Context::InvalidateAllWindows()`。
- `TitleBarWidget::SetTitle/SetTitleWeight`、`DialogWidget` 四个文本
  setter 补调 `RequestLayout()`（此前裸赋值，连旗标都不置）。

## 1.6.0 — build 156

### 性能 / 启动
- **ShowImmediate 跳过重复的 FRAMECHANGED**：`PrepareRT` 两分支（premax 的
  `SW_SHOWMAXIMIZED` / 普通分支的 `SWP_FRAMECHANGED`）已完成 frame 确立，
  `ShowImmediate` 里历史遗留的 `SetWindowPos(FRAMECHANGED)` 对 prepared
  路径是纯重复的 DWM 事务（实测 ~10ms）。新增 `framePrepared_` 旗标：
  prepare 路径跳过；未经 PrepareRT 的独立 `ui_window_show_immediate`
  调用方行为不变（仍触发 NCCALCSIZE，borderless 客户区语义不受影响）。
  GuoheView 启动实测 229~263 → 223~224ms（窗图同现保持）。

## 1.6.0 — build 155

### 性能 / 启动
- **共享 D3D11 设备后台预热**：`ui_init`/`ui_init_with_theme` 即启后台线程
  预创建共享 D3D11 设备（`D3D11CreateDevice` 为 free-threaded，可跨线程移交；
  D2D 设备仍在 UI 线程由 single-threaded factory 创建）。设备创建与 uix
  编译/窗口创建并行，首窗 `CreateRenderTarget` 不再在关键路径上支付
  ~39ms 设备创建（GuoheView 启动剖析实测 window-open 段 113→90ms）。
  `EnsureSharedDevice` 经条件变量收割预热结果，未预热/预热失败时原地
  创建，行为与旧版一致。幂等，多次 `ui_init` 无害。

## 1.6.0 — build 154

### 修复 / SVG 渲染

- **SVG 多 `<tspan>` 文字修复（GuoheView L122）**：matplotlib 等导出的 SVG 把坐标轴标题
  拆成逐字符 `<tspan x=.. style=..>`、靠父 `<g rotate>` 旋转。此前 `ExtractInnerText`
  把 `<tspan>` 之间源码的换行/缩进空白当成换行符 → 文字被拆成"每字一行"竖排，再被父
  `<g>` 旋转翻转（横竖颠倒）；且 font-size/family/font-style 写在 tspan 上时被忽略，
  退回默认字号、丢 italic。现 `SvgInlineTextAsPaths` 把每个 `<tspan>` 当"带定位的子
  run"：按各 tspan 的 `x`/`y`(+`dx`/`dy`) 定位、字体/样式自带优先否则继承，只取 tspan
  自身文本（不含 tspan 间空白），按 fill 分组生成 `<path>` 原地内联（z 序与 L121 一致）。
  - 新增 `font-style`(italic/oblique) 解析，纯文本路径同样支持。

## 1.6.0 — build 153

### 修复 / SVG 渲染

- **SVG `<text>` z 序修复（GuoheView L121）**：D2D `ID2D1SvgDocument` 只画形状、不渲染
  `<text>`，core-ui 原先用 DirectWrite 在所有形状画完之后统一叠加文字。当 SVG 把形状与
  文字交错排列（后画的形状本应遮住先画的文字，如 PPT/Office 导出的 FT-Transformer ×4
  堆叠卡片）时，被遮挡的后层文字也被叠到最上层 → 重叠、溢出卡片。
  现改为预处理阶段用字形轮廓（`GetGlyphRunOutline`）把每个 `<text>` / `<foreignObject>`
  转成等价 `<path>` 原地内联回 DOM，交给 `DrawSvgDocument` 按文档顺序统一渲染 —— 文字
  与形状回到同一条有序管线，z 序天然正确；文字渐变 / 裁剪 / 混合也一并跟形状走 D2D。
  - 新增 `Renderer::SvgInlineTextAsPaths`（+ 字形→SVG path-data sink + 自定义
    `IDWriteTextRenderer`，复用 layout 定位 / anchor / baseline + 字体回退）。
  - `gh_img_view` / `image_source_svg` 两条 SVG 文件渲染路径在 `CreateSvgDocument` 前
    接入转换，移除独立 DirectWrite 文字叠加 pass。

## 1.6.0 — build 152

### API / 控件查询

- 新增 **通用控件属性查询** `ui_widget_get_basic` / `ui_widget_get_text` (GuoheView
  反馈)。此前 lib 只有 `ui_label_set_text` 没有 get, 业务读不到控件文本 (如"点击复制
  label 值")。补:
  - `ui_widget_get_text(w, buf, cap)`: 直接取任意文本控件的文本写进 buf (utf-16),
    返回长度 (> cap-1 即截断)。覆盖 Label/Button/Input/Combo/TextArea/Toggle/
    Radio/Check/Nav/Overlay/TitleBar。
  - `ui_widget_get_basic(w)`: 精简 json `{id,type,text}` (比 ui_debug_dump_widget
    轻, 无 rect/metrics/children), 一次拿控件名称/类型/值; free 用 ui_debug_free。
  内部复用 DebugDumpTree 已有的 type/text dynamic_cast 链 (新增 WidgetTextValue /
  WidgetBasicJson, ui_debug.cpp), 不重复造轮子。ABI 增量 (新增 2 个 C API, 不破坏现有)。

## 1.6.0 — build 151

### 菜单 / 多屏

- 修复 **副屏(非主显示器)右键菜单 / 菜单栏下拉 / submenu 弹到主屏** (L120, GuoheView
  反馈)。根因: `ContextMenu::ShowPopup` 用 `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`
  (主屏尺寸) 把菜单位置 clamp 到 `[0, 主屏宽]`, 副屏的菜单坐标 (screenX 可能 > 主屏宽
  或 < 0, 由 ShowMenu 的 ClientToScreen 正确算出) 被硬拉回主屏。修复: 改用
  `MonitorFromPoint(MONITOR_DEFAULTTONEAREST)` 拿鼠标所在屏 → `GetMonitorInfoW` 取
  `rcWork` 做 clamp (顺带避开任务栏), 拿不到 monitor info 时 fallback 主屏。所有走
  ShowPopup 的菜单 (右键菜单 / 菜单栏下拉 / 子菜单) 一并修复。DPI 仍用
  `GetDpiForWindow(parentHwnd)` (主窗与菜单同屏, 正确)。

## 1.6.0 — build 150

### 布局 / 样式 (recomputeStyle)

- 修复 **首次 hover/press/focus 触发 recomputeStyle 时, "CSS 没写 gap 的多 child
  容器" 在首次交互瞬间布局跳变** (L119, GuoheView 反馈)。根因: `recomputeStyle`
  (compiler.cpp, hover/press/focus 时重算样式) 把 VBox/HBox 的 gap 无条件重设成 `0`
  再让 CSS 覆盖, 而 `ApplyFlexContainerStyle` 只在 CSS 显式写了 gap 时才覆盖 —— 于是
  容器编译时用默认 gap=4、首次交互后变 0, 间距塌缩、子元素位置跳变 (之后稳定不动)。
  修复: 新增 `kDefaultFlexGap = 4.0f` 常量 (widget.h), VBox/HBox/Grid 默认 gap 与
  recomputeStyle 的重设值统一从它来 —— 编译默认值与重设值永不打架。
  **零破坏** (默认值不变, 现有布局本就按默认 4 设计)。
  典型症状: GuoheView toolbar 右侧按钮组首次点击右移 8px、之后固定。

## 1.6.0 — build 149

### gh_img_view / 渲染

- 修复放大切金字塔级时画布"波浪一样"逐 tile 刷新 (L115, GuoheView 反馈)。两个根因:
  ① `SetTile` 每个 tile 都 `InvalidateAllWindows` → N tile = N 次整窗重绘;
  ② L48 的 `TrimToViewport_` 切级瞬间删光旧级 tile, 废掉了 OnDraw 本有的多级
  fallback (新级缺 tile 时透出旧级覆盖), 只剩最粗 preview → 逐 tile 补 = 波浪。
  修复: ① `TrimToViewport_` 保留上一个 active level 的 tile (新增成员 `prevActiveLevel_`,
  `SwitchLevel` 切级时记录), OnDraw 多级 fallback 用旧级清晰图覆盖新级未到达区 →
  切级"清晰→更清晰"无波浪、无空白; ② 新增 `ui_gh_img_view_begin_tile_batch` /
  `ui_gh_img_view_end_tile_batch`: 宿主把整级可见 tile 攒齐一次 batch 提交, batch
  内不逐个 invalidate、end 时一次刷。旧级 tile 内存约新级 1/4 (稳态 2 级 viewport
  tile, 可忽略)。ABI 增量 (新增 2 个 C API, 不破坏现有)。

## 1.6.0 — build 148

### gh_img_view / 渲染

- `ui_gh_img_view` 非整数缩放下瓦块边界的半透明接缝彻底消除 (L105)。两类缝:
  ① **分数设备像素部分覆盖** —— 瓦块 dest rect 是 DIP, ctx 按 dpi_scale_ 缩到设备像素,
  DPI≠100% 时边界落分数设备像素, D2D 对每块位图边缘做部分覆盖, 相邻两块在共享边界处
  SrcOver 合成总覆盖 <100% → 透背景半透明缝。修复: dest rect 四条边 snap 到整数【设备
  像素】(round(v*dpr)/dpr), 相邻块共享边界同表达式 → 同设备整数 → 精确贴合。
  ② **插值梯度不连续** —— 每块瓦块独立插值、在自己边缘钳位, 跨瓦块采样不连续的残留细缝。
  修复: `DrawLevel` 先把本级可见瓦块 1:1 精确拼到一张连续离屏位图 (无内部瓦块边界), 再
  整体缩放一次上屏, CUBIC/LINEAR 跨越原瓦块边界连续采样。复用 SVG 离屏渲染套路
  (CreateBitmap(TARGET) + SetTarget 切换, BeginDraw/EndDraw 间合法), 缓存离屏位图按需 grow,
  外边界 device-snap, bbox>4096 或无 ID2D1DeviceContext5 时退逐块直绘。无 ABI 形状变化。

## 1.6.0 — build 147

### i18n / 布局

- 运行时切换 locale 后, 更宽语言的 `$t()` label 不再换行/溢出 (L104)。`PageState::SetLocale`
  改 `$locale` 让所有 `$t()` binding 重算文字, 但 `ApplyBindingToWidget` 的 `SetText` 路径
  只 `InvalidateAllWindows()` 重绘、**不 relayout** —— label 保留上一 locale 布局算出的 rect,
  切到更宽的语言 (如 zh "拖入图片到这里" → en "Drop an image here") 时新文字塞进旧窄框就
  换行。表现时序相关: 目标 page 的窗口在前台时常被别的事件 relayout 掩盖, 在后台 (子窗在前)
  切换则卡住不复位。修复: 新增 `Context::RelayoutAllWindows()` (对称于 `InvalidateAllWindows()`,
  对每个 window 调 `LayoutRoot()`+`Invalidate()`), `SetLocale` 末尾 bulk 文字更新后调用一次,
  令所有 label 按新译文重新测量固有宽度 (`LabelWidget::SizeHint` 实时测量), 与窗口前台/后续
  paint 无关。一次 relayout 而非逐 binding, 避免一次切语言几十条 binding 触发几十次全树布局。

## 1.6.0 — build 146

### Widget / 布局

- 最大化启动彻底消除"白条闪"——用 `WS_EX_LAYERED` alpha 0 让 maximize 全程不可见
  (接 build 145)。build 145 的 maximize+hide 在 `PrepareRT` 那下 `SW_SHOWMAXIMIZED` 会把
  还没建 RT/绘内容的窗口短暂合成出来一帧: 黑底 + 顶部最大化窗自带的 1px 白边线 = 用户
  看到的"白条闪"(这条白线是窗口顶部边线, 不是 borderless DWM margins, 改 `margins{0,0,0,0}`
  治不了)。现在 `PrepareRT` 真最大化前先给窗口 `WS_EX_LAYERED` + `SetLayeredWindowAttributes`
  (alpha 0) 设透明, `SW_SHOWMAXIMIZED` 在透明态完成 → 屏幕上完全不可见, 无黑底/白边闪;
  窗口保持 shown + alpha 0, `ShowImmediate` 绘完内容后一次 `SetLayeredWindowAttributes`
  (alpha 255) 揭示 → 首帧即全屏 + 有内容。一次解决最大化启动全部视觉问题: 图片 fit(L101)、
  拖标题/双击还原(L102)、无顶部白条(L103)、无"画布/toolbar 先小矩形再全屏"。实测窗口
  可见耗时 ~343ms(无 layered 首次显示 500ms 惩罚)。仅 `start_maximized` 命中时生效。

## 1.6.0 — build 145

### Widget / 布局

- 最大化启动改用"真最大化 + 隐藏"(maximize+hide), 首帧即全屏 (接 build 144)。build 144
  用 resize-to-N(`ShowImmediate` 真 maximize 前把窗口 `SetWindowPos` 回常规矩形)修好了
  拖标题还原 + 白条, 但 `SW_SHOWMAXIMIZED` 会先把窗口在常规尺寸 reveal 再放大, 那一帧
  常规尺寸矩形可见 → 画布/toolbar"先在小矩形按 0,0 画一帧再变全屏"。现在 `PrepareRT`
  对创建时的常规尺寸窗口直接 `ShowWindow(SW_SHOWMAXIMIZED)` + `SW_HIDE`("真最大化再藏回",
  两次 `ShowWindow` 之间不泵消息→DWM 不合成可见帧→不闪)。窗口此刻 = 最大化态 + hidden:
  `client`=工作区、`rcNormalPosition`=常规创建尺寸、`maximized_`=true(`margins{0,0,0,0}`)。
  `ShowImmediate` 改用 `SW_SHOW` 直接 reveal 这个已最大化的窗口 → 首帧即全屏。一次解决:
  ① 图片 fit 按最大化算(无"先小后大", L101); ② 拖标题/双击还原到常规窗口(L102);
  ③ 无 1px 顶部白条(L103); ④ 无"画布/toolbar 先小矩形再放大"。删掉 build 144 的
  `startMaxNormalRect_` 字段 + resize-to-N(maximize+hide 不需要)。仅 `start_maximized`
  命中时生效, 常规启动零影响。

## 1.6.0 — build 144

### Widget / 布局

- 开局最大化(`start_maximized`)时拖标题能还原 + 消除顶部白条 (L102/L103, 接 build 143)。
  build 143 的 `PrepareRT` 为让 caller 的图片 fit 按最大化尺寸算, 把窗口 `SetWindowPos`
  到工作区, 使窗口进入"工作区尺寸 + 常规状态"的畸形态, 引出两个回归: ① `ShowImmediate`
  的 `SW_SHOWMAXIMIZED` 把 `WINDOWPLACEMENT.rcNormalPosition`(拖标题/双击还原矩形)记成
  工作区 → 开局最大化拖标题"还原"到同尺寸、看不出变化; ② 工作区全宽的常规态 borderless
  窗有 `MARGINS{0,0,1,0}` 的 1px 顶部 DWM frame, reveal 时横跨屏幕闪一条白边。
  现在 `PrepareRT` 缩到工作区**前**抓住创建时的常规窗口矩形, `ShowImmediate` 在
  `SW_SHOWMAXIMIZED` **前**把窗口 `SetWindowPos` 回该常规矩形 → maximize 成为真转换
  (常规矩形=创建尺寸≠工作区): `rcNormalPosition`=常规尺寸(拖标题/双击还原到常规窗口),
  且窗口 maximize 前是常规小尺寸且 hidden、reveal 直接到最大化态(`margins{0,0,0,0}`),
  从不以全宽常规态露面(无顶部白条)。`PrepareRT` 仍把画布布到工作区, build 143 的图片
  fit 效果(无"先小后大")完整保留。仅 `start_maximized` 命中时生效, 常规启动零影响。

## 1.6.0 — build 143

### Widget / 布局

- `PrepareRT` / `Show` 现在认 `start_maximized` hint (L101)。带 `start_maximized` 的
  prepare 路径 (`ui_page_prepare_window` → `PrepareRT`) 此前按窗口当前常规 prepare 尺寸
  建 RT + `LayoutRoot`, 不理会 `startMaximizedPending_`。caller 在 show 前做的布局/图片
  fit 因此按常规尺寸算, 随后 `ShowImmediate` 的 `SW_SHOWMAXIMIZED` 把画布撑到最大化 →
  首帧内容按常规尺寸 fit (偏小/偏位) 再被 resize 重 fit (可见跳变)。现在 `PrepareRT` 在
  hint 命中时先把隐藏窗口 `SetWindowPos` 到最大化客户区 (= 显示器工作区, 同
  `OnGetMinMaxInfo`) 再建 RT/`LayoutRoot`; `Show()` 的 preMaximized 分支对"hint 尚未
  zoom"子情况同样先预置工作区再 `OnPaint`。这样 prepare 阶段画布即最终最大化尺寸,
  caller 的 fit 一次算对, 随后 `SW_SHOWMAXIMIZED` 同尺寸无 reflow, 消除"先常规一帧再
  放大"。仅 `start_maximized` 命中时改尺寸来源, 常规启动零影响。

## 1.6.0 — build 142

### Widget / 布局

- 内置 `.menuitem-row label` 现在默认 `white-space: nowrap` (L100, 接 build 141)。
  build 141 给 HBox 加了 flex-shrink, 长 `<label>` 会被收缩到行宽; 但 `<label>`
  默认 `wrap_=true`(浏览器式软换行), 收缩后会折成多行、撑高 item 竖向溢出压到
  相邻菜单项(正是 L100 报告的"文字超出菜单"+"有的文字会换行")。菜单项按约定是
  单行, 内置 CSS 补 `white-space: nowrap` 后 `wrap=false` → renderer 自动启
  ellipsis trimming, 配合 flex-shrink 收缩后的行宽, 长名走单行省略号
  `Adobe Photoshop CC 2024…`。需要多行菜单项的调用方用户 CSS 覆盖
  `white-space: normal`。
  - 实测: 注入超长应用名到 dogfood "打开方式"子菜单, 修复前 3 行换行+竖向溢出,
    修复后单行省略号、item 等高。

## 1.6.0 — build 141

### Widget / 布局

- `HBoxWidget` 现在支持 CSS `flex-shrink` (L100)。此前 HBox 给非 flex 子节点分配
  内容/文字自然宽, 只做 min/max clamp, 内容超过容器宽时**不收缩**直接溢出; 定宽行
  (如 `.menuitem-row { width:100px }` 的菜单项)里超长 `<label>` 会撑出卡片、且因
  label rect ≥ 文字宽, `NO_WRAP`+ellipsis trimming 永不触发(没省略号)。现在
  `HBoxWidget::DoLayout` 在 `contentW > totalW` 时按 `flexShrink × width` 比例把
  可收缩子节点收缩到 `minW`(默认 0), 迭代分配触底项的剩余收缩量(CSS-correct)。长
  label 正常省略号截断, 不再溢出菜单卡片。
- `flex-shrink` / `flex: none` / `flex` 简写的 shrink 分量现在被 `ApplyFlexItemStyle`
  解析。此前完全忽略 → 之前写的 `flex-shrink:0` / `flex:none` 是空操作, 现在生效。
- **BREAKING**: 默认 `flex-shrink: 1`(对齐 CSS)。所有 HBox 子节点在行内容溢出时都会
  收缩。需要保持原尺寸的子节点(图标等)用 `flex: none` 或 `flex-shrink: 0` 豁免。
  作用域仅 HBox(横向); VBox 纵向暂未支持收缩。

## 1.6.0 — build 140

### 输入 / IME

- `WM_KEYDOWN` 现在解析 `VK_PROCESSKEY` 拿真实键 (L96)。IME(中文等)激活并正在
  处理该键时, Windows 把 `WM_KEYDOWN` 的 `wParam` 设成 `VK_PROCESSKEY`(0xE5),
  真实键不在 wParam 里。`HandleMessage` 之前 `int vk=(int)wParam` 原样下发, 导致
  按 VK 的消费者(如 dogfood 端快捷键捕获)拿到 0xE5、映射不出、显示成 `?`。现在
  `vk==VK_PROCESSKEY` 时从 `lParam` 扫描码 `MapVirtualKeyW(sc, MAPVK_VSC_TO_VK)`
  反查真实 VK(走扫描码、IME 无关、user32 已链接, 不引 imm32 依赖)。IME 文字输入
  仍走 `WM_CHAR`/`WM_IME_CHAR` 另一条路, 不受影响。

## 1.6.0 — build 139

### Widget / 布局

- `ui_widget_set_visible` 改可见性现在立即生效 (L95)。此前它只翻 `visible` 标志、
  不请求重布局, 而兄弟 setter (`set_size`/`set_expand`/`add_child`) 改完都调
  `MarkLayoutAndRepaint`(`RequestLayout` 标脏 + `InvalidateAllWindows`)。改可见性
  会折叠/展开 flex 布局, 同样需要重布局——漏了这行导致控件隐藏时被算成 `0×0`
  rect, `set_visible(0→1)` 后标志虽 true 但 rect 卡在 0×0, 要等别的 relayout
  (如重开窗口/拖窗)才显示。现在 `set_visible` 改值后也调 `MarkLayoutAndRepaint`
  (值未变则短路), 跟兄弟 setter 一致。下游不再需要 set_visible 后手动 relayout
  或改用 opacity 的 workaround。

## 1.6.0 — build 138

### 窗口 / DPI

- `on_resize` 回调单位补统一为 **DIP** (L91)。`UiWindowImpl::OnResize` 此前把
  `WM_SIZE` 的物理像素直接丢给 `onResize` 回调，是 lib 里唯一还传物理像素的
  窗口尺寸出口——L6/L23/v1.2.0 把窗口几何 get/set API + widget rect 都统一成
  DIP 时漏了它。后果：消费者按 DIP 假设接收（跟 `ui_window_set_size`/
  `ui_widget_get_rect` 一致），在非 100% DPI 屏把物理值当 DIP 用，再经
  `set_size` 错乘 dpiScale（典型：dogfood 端无边框看图拖小窗后切图，窗口被
  ×DPI 放大、记不住长边）。现在 `OnResize` 调回调前 `width/height ÷ dpiScale_`
  转 DIP，跟其它窗口/widget 尺寸 API 全一致。**BREAKING（理论上）**：依赖
  `on_resize` 给物理像素的消费者需改；本仓 demo/examples 无源码消费，无影响。

## 1.6.0 — build 137

### 菜单 / 右键

- 右键菜单两处修复 (L90)。① **隐藏的 trigger widget 不再抢右键菜单**：右键派发
  此前只按 `triggerElement->Contains(x,y)` + 深度最深匹配，不查可见性，导致
  `set_visible(0)` 的 widget（rect 仍在）仍抢菜单——典型：dogfood 端 borderless
  看图时隐藏的鸟瞰图(minimap)在小窗下盖住大半画布，右键弹出隐藏控件的菜单。
  现在两处 rclick 派发（`WireSubtreeMenus` + `AttachWindow`）匹配时沿父链查有效
  可见性，自身或祖先 `visible=false` 则跳过。② **canvas-mode 下点击非客户区
  现在能关菜单**：`OnMouseDown`(HTCLIENT) 里有 `CloseMenu`，但 `EnableCanvasMode`
  把画布判成 HTCAPTION（整窗可拖），左键走 `WM_NCLBUTTONDOWN`→DefWindowProc
  窗口移动 loop，不经 `OnMouseDown`，菜单关不掉。现在 wndproc 的
  `WM_NCLBUTTONDOWN` 在有 active menu 时先 `CloseMenu` 并消掉点击。

## 1.6.0 — build 136

### SVG

- `<text>` 叠加渲染补 **font-weight / 渐变 fill / 继承 opacity** (L87)。D2D
  `ID2D1SvgDocument` 不画 `<text>`，core-ui 用 DirectWrite 叠加补渲（L75），但
  叠加路径此前只支持纯色 + 固定字重 + 满不透明，丢三样：① `font-weight` 没解析
  → 不加粗；② `fill="url(#id)"` 渐变 → `ParseSvgColor` 失败退到黑；③ 父
  `<g opacity>`/`fill`/`font-*` 没继承（如 `INTP/v20230721` 该继承父组
  `fill=#EEEEEE` 却渲染成黑）。修复：`SvgTextRun` 加 `fontWeight/opacity/
  hasGradient`＋`SvgTextGradient`；新 `ParseGradients`（扫 `<linear/
  radialGradient>`，含 `href`/`xlink:href` 的 stop＋几何继承 resolve）；
  `ExtractTextRuns` 的 `<g>` 栈升级成完整文字属性级联（transform＋opacity＋
  fill＋font-size/weight/family）；`DrawSvgTextRuns` 传字重＋按文字 bbox 建
  linear/radial 渐变 brush（objectBoundingBox 先过 gradientTransform 再映
  bbox，userSpaceOnUse 直接用户空间）＋opacity 乘进 alpha。**限界**：
  per-`<tspan>` 多段不同 fill 暂不做（需 DirectWrite per-range drawing effect）。

## 1.6.0 — build 135

### SVG

- `<use href>`（无 `xlink:`）与 `<symbol>` 现在能在 D2D 渲染了 (L86)。D2D
  `ID2D1SvgDocument` 是 SVG 1.1 子集，两个坑会让引用型内容整组消失：① 只认
  `xlink:href`，不识别 SVG 2 的裸 `href` —— `<use href="#id">` 整组静默不渲染；
  ② 完全不支持 `<symbol>` —— 即便 use 用 `xlink:href` 指过去也一概不画。雪碧图
  / 图标集常用的 `<symbol>` + `<use href>` 双重踩坑（如半导体果冻.svg 的芯片
  金属针脚整组不见）。新增 `NormalizeSvgRefsAndSymbols` 预处理 pass（挂进
  `LoadSvgWithInlinedStyles`，D2D 原生路径 + `ParseSvgIcon` fallback 共享）：
  元素有 `href` 无 `xlink:href` → 镜像补 `xlink:href`（保留原 `href`），root
  `<svg>` 补 `xmlns:xlink`；`<symbol>` → `<g>`，顶层 symbol 外套 `<defs>` 防
  单独绘制，已在 defs 内则只换名。**限界**：symbol 自带 viewBox 且靠 use 的
  width/height 做视口缩放的场景不复刻视口变换；无 viewBox 的 symbol（绝大多数
  雪碧图）完全修复。新增 `test/src/svg_normalize_test.cpp` 6 用例。

## 1.6.0 — build 134

### i18n

- `<select><option>@key</option>` 现在支持翻译 (L83)。此前 `<option>` 文本被
  compiler 收成静态 ComboBox item 列表, 不走 `$t` 反应式 —— `@key` 显示字面 key,
  `{{$t}}` 渲染空, 而同页 `<label>`/`<button>` 的 `@key` 正常。现在 compiler 用
  `AsI18nKey` 识别 `@key` option, 记 i18n key + 用 key 作占位; `ComboBoxWidget`
  加 `i18nKeys_` + `SetI18nItemKeys`/`RetranslateItems`; `PageState::SetLocale`
  切换时遍历树对 combobox 重译 (首次 set_locale 解析占位)。纯新增, ABI 不变,
  不带 `@key` 的 option 行为完全不变。

## 1.6.0 — build 133

### Fix — SVG 文字也渲染到 minimap / 缩略图 (L75 补完)

build 132 的 SVG 文字渲染只覆盖了主视图（`gh_img_view` OnDraw）和 `image_source`，
漏了 `gh_img_view::RenderSvgToBgra` —— 鸟瞰图（minimap）/ 缩略图走这条离屏光栅化
路径，`DrawSvgDocument` 之后没叠加文字 → 放大后 minimap 里框有字缺（主视图有字、
minimap 没字，不一致）。

`RenderSvgToBgra` 在 `DrawSvgDocument` 后加 `DrawSvgTextRuns`（同 Scale 变换，画到
离屏 bitmap）。现 core-ui 全部 3 个 `DrawSvgDocument` 调用点（OnDraw / RenderSvgToBgra
/ SvgSourceNative）都叠加文字，覆盖一致。

## 1.6.0 — build 132

### Feat — SVG `<text>` / `<foreignObject>` 文字渲染 (L75)

打开 SVG 文件时现在会渲染文字。此前 SVG 走 D2D `ID2D1SvgDocument`（shapes-only，
不支持文字），Mermaid / draw.io / Graphviz / PlantUML 等图表 SVG 打开只有框线和
连线、**所有文字标签全缺**。

core-ui 现自己解析 SVG 文字 + DirectWrite 叠加补渲（跟形状同一个 user→screen 变换，
对齐 + 任意缩放矢量清晰）：

- `<text>` / `<tspan>`：x/y + font-size/font-family/fill + text-anchor
  (start/middle/end) + baseline 定位。覆盖 draw.io / Graphviz / PlantUML。
- `<foreignObject>`（内嵌 XHTML）：抽内部 div/p/span 文字（剥标签 + 解 HTML 实体 +
  `<br>`/`</p>` 换行）+ 从 style 取字体色 + 锚点设框中心居中。覆盖 Mermaid。

新增 API：`Renderer::ParseSvgTextRuns(xml)` + `Renderer::DrawSvgTextRuns(runs, baseXf)`
+ `SvgTextRun` 结构 + `SvgIcon.textRuns`。`gh_img_view` 与 `image_source_svg` 两条
SVG 渲染路径都接上。

**子集**：foreignObject 只取文字，不做完整 HTML 布局（不支持表格/图片/复杂 CSS）。
**非破坏性**：纯新增能力，旧 SVG（无文字 / 只有形状）渲染不变。

## 1.6.0 — build 131

### Fix — `<img>` 支持 width/height 属性，跟 `<svg>` 一致 (L74)

`<img>` 现支持 `width` / `height` 属性（写进 `fixedW` / `fixedH`），跟 `<svg>` 分支一致。
此前 widget_factory 的 img 分支只读 `src` / `object-fit`，`<img width="18">` 被忽略、
img 用图片固有尺寸（撑爆父布局），必须靠 CSS 才能控尺寸。
`ImageWidget::SizeHint()` 早已支持 fixedW/H（>0 时优先于固有尺寸），纯属 factory 漏解析。
CSS `width` / `height` 仍可在 layout 阶段覆盖。

**非破坏性**：旧 `<img width>` 本就不生效，无 caller 依赖旧的“忽略”行为，故普通 build bump（不标 BREAKING）。

## 1.6.0 — build 130

### BREAKING + Feat — 菜单项支持字符串 id + 点击回调暴露全部属性 (L73)

`<menuitem id="...">` 的 id 从"只能整数"升级为任意字符串 key（如 `id="cmd_delete"`），
跟其它 widget 的 id 用法一致；数字 id 仍兼容（当字符串 `"1000"` 处理）。菜单点击回调
`UiMenuCallback` 的 `int item_id` 改为 `UiMenuItem` 不透明句柄（**C ABI BREAKING**），
配 `ui_menu_item_id(item)` 读 id、`ui_menu_item_attr(item, name)` 读任意静态属性
（含 `data-*` / `shortcut` 等）。点击载荷经 WM_APP+100 用 heap `MenuClickInfo` 传递，
回调读完即 delete，解决菜单 popup transient 的生命周期。

**动机**：老设计宿主把菜单数字 id 直接 `static_cast` 成命令枚举整数值，宿主枚举里
插一个命令就让 ≥ 该值的所有菜单 id 错位（下游撞过）。字符串 key + 属性让菜单项
自描述、宿主按 key / `data-*` 路由，彻底脱钩枚举顺序。

**迁移**：宿主 `ui_window_on_menu` 回调签名改 `(UiWindow, UiMenuItem, void*)`，用
`ui_menu_item_id` / `ui_menu_item_attr` 取值；`<menuitem id="N">` 可改成有意义的
字符串 key（数字 id 不改也兼容）。内部 hit-test / debug API（`ui_debug_menu_click_id`
等）/ `@click` JS 路由仍工作（`@click` 改按 strId 键）。

## 1.6.0 — build 129

### Fix — tooltip 沿父链上溯查找 owner (容器 set_tooltip, hover 子级也弹) (L72)

`ui_widget_set_tooltip` 设在容器 widget（如图标按钮 `<div>`）上时，hover 其内部子
widget（如 `<svg>` 子级）不弹 tooltip —— `OnMouseMove` 调度 timer + `WM_TIMER`
显示都只看最深命中 widget 的 `tooltip`（`hit->tooltip`），不沿父链上溯；而 hover
状态传播 + cursor 继承都已沿父链，行为不一致。

修复：两处都从 hit / `hoveredWidget_` 沿父链 `while (w && w->tooltip.empty())
w = w->Parent()` 上溯到最近一个有 tooltip 的祖先取 owner（跟 cursor 继承同模式）。
容器 `set_tooltip` 后 hover 内部任意子级都能弹。

## 1.6.0 — build 128

### Fix — 反应式 SVG `:fill`/`:stroke` 在 hover/state 重渲染后保持 (L71)

path 级反应式 `:fill`/`:stroke`（`:`-前缀 binding）设的颜色，在 hover/press/focus/
类切换触发的 `recomputeShapes`（从 static 属性 + CSS 重建 shape）后丢失 → 反应式
SVG 图标一被 hover 就变透明消失，关窗重开又正常。

**根因**：`recomputeShapes`（compiler.cpp）把每个 shape 重置为 `fresh`，只重新应用
static 属性 + CSS，**跳过 Bind 属性**；binding 经 `SetShapeProperty` 设的运行时值在
`shapes[i] = move(fresh)` 被覆盖丢失，而 binding 仅在表达式*变化*时才 re-apply，hover
不触发它。menu 项不撞是因菜单每次弹出全量重建，走初次编译路径。

**修复（通用）**：`SvgWidget` 缓存 `SetShapeProperty` 设的值（`boundShapeAttrs_`）+
新增 `ReapplyBoundAttrs`；`recomputeShapes` 重建 `fresh` 后、写回前补回缓存的 bind
值（最高优先级，覆盖 static/CSS）。所有反应式 SVG 属性（`:fill`/`:stroke`/`:cx`/`:d`
等）在 hover/press/focus/类切换重渲染后都保持。

## 1.6.0 — build 127

### Fix — gh_img_view drag 期间 fire onMouseMoveHook (宿主可观察拖动)

gh_img_view 此前 `OnMouseMove` 只做 pan, 不 fire 基类 `onMouseMoveHook`; 而
窗口在 pressed (拖动) 分支直接调 widget `OnMouseMove`、也不 fire hook (仅 hover
路径 fire). 结果: 宿主用 `ui_widget_on_mouse_move` 挂的回调在"按住拖动"期间
完全收不到 move —— 无法实现"画布图片拖出到其它应用"之类需要观察拖动的手势
(ImageViewWidget 一直有自己的可消费 hook, gh_img_view 缺这条).

修: `GhImgViewWidget::OnMouseMove` 在 pan 前 fire `onMouseMoveHook(e)`, 之后
复检 `dragging_` —— 宿主若在 hook 内发起 `DoDragDrop`, 其夺 capture 触发
build 126 的 `WM_CAPTURECHANGED` → `CancelMouseCapture` 复位 `dragging_`,
`DoDragDrop` 返回后复检为假即跳过 pan, 拖出后图不被 pan 走. 不发起拖出时
`dragging_` 仍真, pan 照常.

## 1.6.0 — build 126

### Fix — WM_CAPTURECHANGED 复位 press 中的 widget (capture 被夺不再卡 drag 态)

窗口此前不处理 `WM_CAPTURECHANGED`。当鼠标 capture 被外部夺走 (典型: 宿主在
press 中的 widget 上发起 `DoDragDrop`, 或系统/其它进程夺取) 时, 被按住的 widget
(gh_img_view / image / slider / scrollview / splitter) 收不到 `WM_LBUTTONUP`,
导致 `pressedWidget_` 与其内部 drag 状态 (如 gh_img_view `dragging_`) 卡死 ——
之后无按键 hover 移动仍被路由到该 widget 的 `OnMouseMove`, 表现为图片跟着鼠标
误 pan。

新增 `WM_CAPTURECHANGED` 处理 → `CancelMouseCapture()`: 复位 `pressedWidget_`
+ 调其 `OnMouseUp` 取消内部 drag 状态, 但**不 fire onClick** (capture 被夺是
取消语义, 非点击释放), 也不再 `ReleaseCapture` (capture 已易主)。正常
`ReleaseCapture` 流程因 `pressedWidget_` 已清空而 no-op, 不重复触发。

典型受益场景: 宿主实现"画布图片拖出到其它应用" (press 画布 → 越界 → DoDragDrop)。

## 1.6.0 — build 125

### Feature — gh_img_view 采样模式 + 滚轮缩放开关

新增两组 gh_img_view API，给宿主更细的画布交互控制：

- `ui_gh_img_view_set_antialias(w, on)` / `ui_gh_img_view_get_antialias(w)`
  控制放大时的采样模式。`on=1`（默认，保持原有行为）放大走 HQ_CUBIC/LINEAR
  平滑；`on=0` 放大走 NEAREST_NEIGHBOR，像素边界清晰（像素画 / 逐像素查看）。
  下采始终走平滑路径不受影响。影响 preview 与 tile 两条 DrawBitmap 路径。
- `ui_gh_img_view_set_wheel_zoom_enabled(w, on)` /
  `ui_gh_img_view_get_wheel_zoom_enabled(w)`
  控制 widget 内部 OnMouseWheel 是否缩放。`on=1`（默认）滚轮缩放；`on=0`
  内部不缩放，宿主用 `ui_widget_on_mouse_wheel` hook 完全接管滚轮（缩放 /
  切图等自定义）。命令式缩放 API（set_zoom / set_zoom_around / fit 等）不受
  此开关影响。

两个开关默认值都保持原有行为，对现有调用方零影响。

## 1.6.0 — build 124

### Feature — L57: ui_window_set_aspect_lock + WM_SIZING 拦截 (拖窗 aspect 锁定)

新 API:

```c
UI_API void ui_window_set_aspect_lock(UiWindow win, int ratio_w, int ratio_h);
```

锁定窗口 aspect 比例 — 用户拖窗任意一条边 / 角 resize 时, lib 在 WM_SIZING
收到 user 拖出的 RECT 后按 ratio 算合法 size 写回, Win32 把这个 RECT 当 user
实际拖的 size 处理. 拖横向 (left/right) → 按 w 算 h; 拖纵向 (top/bottom) →
按 h 算 w; 拖角 → 按拖动幅度大的那边为主.

**用例**: 看图器 borderless 模式 image 严格填满 widget = window. enter 时调
`set_aspect_lock(win, img_w, img_h)`, 切图时更新, exit 时 `set_aspect_lock(win, 0, 0)`
解锁.

**修的什么 bug**: L52-L53 设计 borderless image 严格填满 widget = window 用
viewport callback 在 widget rect 变化时算 target window size + set_size. 但
lib widget OnDraw 检测 rect 变化时也 fire NotifyViewport (典型 user 拖窗
resize 路径), zoom_ field 没变, application 端用旧 zoom × image 算 target =
拖前 size, set_size 把 user 拖的 size 立刻拉回. 主线程被反复 WM_SIZE 阻塞
→ IPC 不响应 → widget paint 停 → 用户看到 "图片消失". GuoheView L57 实测.

WM_SIZING 拦截在 Win32 message 层让 user 拖出的 RECT 已经是合法 aspect,
widget rect 跟 image 严格匹配, viewport callback 拿到的 zoom 跟 widget rect
一致, no-op apply, 不再死循环.

**ratio 单位**: 任意整数, lib 内部用 ratio_w/ratio_h 算 aspect double. 看图
器一般传 image_natural_w/image_natural_h. ratio_w 或 ratio_h 任一 <= 0 =
disable (默认).

实现细节:
- `UiWindowImpl` 加 `aspectLockW_/aspectLockH_` 字段
- WndProc 现有 `WM_SIZING` case 内加 ratio 检查 + RECT 修正 logic
- 没设 lock 时维持老 behavior (fall through 到 default isResizing_ 跟踪)

---

## 1.6.0 — build 123

### Revert — L54: 撤回 build 120/121/122 真透明改造 (回 build 119 baseline)

L54 (build 120) 让主窗口走 composition swap chain + WS_EX_NOREDIRECTIONBITMAP
+ DComp + alpha-aware compositing, 目标是 borderless 模式 image 周围漏出
alpha=0 像素时透出桌面而非 lib 主题色. GuoheView L54 实测引入一连串 UX 回归:

- **1px 边框线**: Win10 WS_THICKFRAME 内核 quirk 留 1px invisible sizing
  border, 在 NOREDIRECTIONBITMAP 模式下 DWM 把它当 default chrome 区. build
  121/122 用 DwmExtendFrameIntoClientArea margins 改 sheet-of-glass 没消掉
- **resize 时 image 消失**: composition swap chain alpha=0 在 widget 重排
  期间没 fallback content. build 119 LWA_ALPHA 模式下 alpha=0 被 DXGI 当
  opaque black, "黑一帧" 而非 "完全消失"
- **焦点切换透明 vs 纯色闪**: NOREDIRECTIONBITMAP + DComp cached frame
  state 跟 focus 切换时序异常

trade-off 评估: 真透明唯一收益是切图 race 期间露的是桌面而非 lib bg, 但
GuoheView L55-B/C 应用层修了 race (on_image_loaded_ reorder + SVG 用 applying
flag 包), 这条收益不再需要. 而 resize 消失 + 焦点闪是 hard 阻塞 UX bug.

revert 全套回 build 119 baseline. 主窗口回 hwnd swap chain + WS_EX_LAYERED
+ LWA_ALPHA + WS_EX_COMPOSITED. EnableCanvasMode 回 build 119 行为 (SetBackgroundMode
+ dragWindow + 隐 TitleBar, 不动 DwmExtendFrameIntoClientArea).

撤回的 commits: a204014 / 261363a / 288a31a / 5a3a26e / b44fd95 / 09db9f5.

build 120-122 期间的 API surface 跟外部 caller 行为:
- `Renderer::SetCompositionOpacity` API 移除 (L54 加的, 没外部 caller 用)
- `Renderer::CreateRenderTargetForLayered` 保留 (popup 仍用), 不再跟主窗口
  共用一条路径
- 老 caller 行为 (ui_window_enable_canvas_mode / `WS_EX_LAYERED` 主窗口)
  全部恢复跟 build 119 一致

---

## 1.6.0 — build 119

### Fix — L53: EnableCanvasMode 画布拖窗 + 右键菜单

1.2.0 引入的 `ui_window_enable_canvas_mode` 在它最核心的目标场景 (root 含
交互子控件: 图片查看器 / 色卡面板 / 桌面挂件) 下其实**不工作**, 两个 bug:

1. **画布拖不动** —— `OnNcHitTest` 走深度优先 hit-test, 只检查叶子 widget
   的 `dragWindow`. `EnableCanvasMode` 把 `dragWindow=true` 设在 `root_`
   上, 但 hit 永远落在 root 的子控件 (如 `gh_img_view`) 上 → 子控件
   `dragWindow=false` → 返 HTCLIENT → 左键被 widget 自己接走变成 pan,
   "整个画布按住可拖窗口" 等于零.

2. **画布右键被系统菜单抢** —— 即使应用层 workaround 把 `dragWindow=true`
   也设到画布 widget 上 (用 `ui_widget_set_drag_window`), 右键经
   `WM_NCRBUTTONUP` 走到 `DefWindowProc` → 弹 Windows 系统菜单
   (Move/Size/Minimize/Maximize/Close), app 自己的 context menu 弹不出.
   → 用户进入画布模式后无法右键退出, 死锁.

**修复**:

1. `OnNcHitTest`: `dragWindow` 检查改 ancestor walk —— `hit → parent → ...
   → root_`, chain 上任一节点 `dragWindow=true` 即返 HTCAPTION.
   `EnableCanvasMode` 设的 `root_.dragWindow=true` 现在真正等价于
   "整个画布可拖", 符合 API 命名直觉, 应用层不再需要手动给所有子控件设
   `dragWindow`.

2. `WndProc` 加 `WM_NCRBUTTONUP` case: HTCAPTION + `onRightClick` 已设时
   调 `onRightClick(dx, dy)` (转 widget tree 右键派发, `WireSubtreeMenus`
   等正常 hook). `onRightClick` 未设时 fall-through `DefWindowProc` →
   老 demo / titlebar 系统菜单行为保留, 不破老 caller.

**后向兼容**: 1.2.0 至今 `dragWindow` 的唯一真实 setter 是
`EnableCanvasMode` (作用在 `root_`), 没有外部 caller 自己手动 set 过
中间节点; 改动 1 安全. 改动 2 用 `onRightClick` 是否设定走分支, 不影响
未挂右键 callback 的 demo / 老应用.

GuoheView L52 borderless 模式撞到这条 — 用户报告 "按住拖动画布无法移动
窗口位置, 一直停在窗体内". 装上 build 119 后, 画布拖窗 + 画布右键弹
"退出无边框" app 菜单两条路径都能用了.

---

## 1.6.0 — build 118

### Fix — L48 续: GhImgView SVG 路径也接 style inliner

build 117 修了 `ImageViewPlus::CreateSvgSourceFromFile` 的 D2D `<style>+class`
解析问题, 但 `GhImgViewWidget::SetSvgFromFile` (`ui_gh_img_view_set_svg_file`)
是平行的另一条 SVG 加载路径, 117 漏掉了它. GuoheView 用的就是 GhImgView,
117 装上以后实测 Adobe Illustrator 导出的 SVG 仍然全黑.

**修法**:
- `svg_style_inliner.h/cpp` 新增 `LoadSvgWithInlinedStyles(path)` 一站式 helper
  (读 UTF-8 文件 + InlineSvgStyleClasses), 两条 SVG 加载路径共用
- `gh_img_view.cpp::SetSvgFromFile` 改成: 调 helper → `SHCreateMemStream` →
  `CreateSvgDocument`, 跟 `image_source_svg.cpp` 流程对齐
- `image_source_svg.cpp` 删掉自带的 `ReadFileUtf8` (跟 helper 重复), 用 helper

build 117 的 release 包不修补 GuoheView 主要场景, 118 装上才完整生效.

## 1.6.0 — build 117

### Fix — SVG `<style>` + class 渲染颜色丢失 (D2D 全黑)

下游 GuoheView L48 反馈: Adobe Illustrator / Figma / Sketch / Inkscape 默认
导出的 SVG (顶部 `<style>.st0{fill:#xxx}</style>` + `<path class="st0"/>`)
进 D2D `ID2D1SvgDocument` 渲染后所有 fill 颜色丢失, 全图纯黑.

**根因**: D2D 的 SVG 实现是 SVG 1.1 严格子集
([MSDN spec](https://learn.microsoft.com/en-us/windows/win32/direct2d/svg-support#unsupported-features)),
不识别 `<style>` 元素内的任何 CSS 规则 (class/id/tag 选择器). 只认元素自身
的 `style="..."` 属性和 presentation attribute (`fill=`/`stroke=`/...).
设计工具默认导出的 style+class 模式撞这个限制 → 现实 80%+ SVG 受影响.

**修法**: 新增 `src/ui/svg_style_inliner.cpp` — 在 `CreateSvgSourceFromFile`
入口加 SVG 文本预处理 pass:

1. 短路: 不含 `<style` 子串直接返回 (零开销, 覆盖所有 inline-fill 图标 SVG)
2. 抽 `<style>` 块 (含 `<![CDATA[...]]>` 包裹), 解析 `.classname { props }`
   形式的 CSS 规则集 → `unordered_map<class, props>`
3. 单 pass scanner 遍历元素的 `class="..."` 属性, 把对应 props 展开成
   元素的 inline `style="..."` 属性
4. 元素已有 inline `style` 时, stylesheet props 前置, 原 inline 后置 ——
   符合 CSS spec: 同优先级后者覆盖前者, 保证原 inline 优先级最高
5. 改写后的 SVG 文本 (mem stream) 喂 `CreateSvgDocument`, fallback
   `ParseSvgIcon` 也复用同一份 xml (附带优化: 原来 D2D 路径直读文件 +
   fallback ReadFileUtf8 二次读盘, 现在只读一次)

**支持范围 (现实 80%+)**:
- ✅ `.classname { fill:#xxx; stroke:#yyy; opacity:0.5 }` 类选择器
- ✅ 逗号分隔多 selector (`.a, .b { ... }`)
- ✅ 多个 `<style>` 块累加; 同 class 多次定义 (后者覆盖前者)
- ✅ `<![CDATA[ ... ]]>` 包裹的 style 内容
- ✅ CSS 注释 `/* ... */`

**Out of scope (留 follow-up)**:
- ❌ `#id` / tag / 复杂选择器 (`.a.b`, `.a > .b`, 属性选择器)
- ❌ `@media` / `@import` / `:hover` / `!important` (遇到整段 skip 不破坏)

**ABI**: 零变化. `ui_gh_img_view_set_svg_file` / `ui_image_view_plus_*` /
`CreateSvgSourceFromFile` 签名全部保持. 下游无感升级.

**性能**: 单 pass O(N), N = SVG 文本字节数. 无第三方依赖. 对无 `<style>`
块的 SVG 零开销 (一次 `find()`).

## 1.6.0 — build 116

### Perf+Fix — GhImgView tile cache 按 viewport 严格管 (内存治理 + 清晰度)

跟用户报告的早期版本"按区加载内存就很小"行为对齐, tile cache 严格跟当前
viewport 绑定, 内存稳态 = viewport 内 tile 数 × 256KB, 不随 zoom 历史累积.

**修复了什么** (GuoheView L47):

0. **window resize 后画布模糊** — caller resize_handler 调 ui_widget_set_size
   只重 layout, 不 fire NotifyViewport → caller 不知道 viewport 变 → 新可
   见 tile 不 enqueue → 用户要手动拖动画布才清晰. 修: GhImgView OnDraw 头
   部检测 widget rect 跟 lastNotifiedRect_ 不同自动 fire NotifyViewport,
   resize 后下次 OnDraw 自动 push 新 viewport tile, caller 透明.

1. **内存累积** — 之前 lib `tiles_` map 无 eviction, set_tile 喂进的 D2D
   bitmap 永远留着. 18641² 卫星 TIF 多次滚轮缩放后实测 fit 76MB → 10 次
   wheel **247MB** (Private), 用户报告"以前 80MB, 现在 200+MB".

2. **caller-lib 状态契约** — caller (GuoheView) 维护 pushed_tiles_ 跟踪"已
   push 过的 tile" 防重复 enqueue. lib 若单方面 evict, caller 不知道 →
   下次 push 跳过该 tile → OnDraw fallback 显 preview / 老 level → 模糊.

3. **SetPan 漏 fire NotifyViewport** — 之前 SetPan inline 只赋值 panX_/Y_,
   不跟 SetZoom 对称 fire callback. caller 调 ui_gh_img_view_set_pan (典型
   minimap click) 后 viewport callback 不 fire → 新可见区不解码 → 用户要
   点击画布触发 OnMouseMove 才清晰.

4. **drag 期间 NotifyViewport 风暴** — 60fps drag 每帧 fire callback →
   caller push 跨 tile 边界的新 tile → worker decode → set_tile UI 线程
   D2D CreateBitmap (~1-2ms/tile) 累积阻塞.

**修法**:

- `Tile` struct 只保留 `bmp / w / h` (撤销 build 116 中间版本试过的 LRU
  字段). `tiles_` map 不再有 byte cap / LRU eviction.
- `NotifyViewport` 内加 `TrimToViewport_(active_level, visible tx/ty range)`:
  清掉 `tiles_` 中 (1) 非 active level 的全部 tile, (2) active level 但
  不在 viewport 范围的 tile. 每个被清的 tile fire `onTileEvicted` callback.
- `SetTile` 改成只装新 tile, 不主动 evict (viewport trim 是 NotifyViewport
  的责任).
- `SetPan` 移到 cpp 实现, 跟 SetZoom 对称 fire `ConstrainPan +
  NotifyViewport + InvalidateAllWindows`. Early return 比较旧值避免无效回调.
- `OnMouseMove` drag 期间完全 skip `NotifyViewport` (避免 60Hz callback
  风暴), `OnMouseUp` drag 结束强 fire 一次精解.

**新增 1 个 C API**:

- `ui_gh_img_view_on_tile_evicted(w, cb, userdata)` — viewport trim 时
  lib 对每个被清 tile 调一次, caller 收到 (level, tx, ty), 同步自己端
  pushed_tiles_ erase. 不注册 → trim 静默. caller 不维护 push 跟踪 set
  的话可以不接.

**实测对照** (D:/测试图片/G0039438 - 副本.tif 18641² 卫星 TIF, fit + 10
次滚轮放大 + 1 次大幅 drag):

```
                           process Private
build 113-115 (无 cap):    247 MB  (zoom 历史累积)
build 116 中间版本 (LRU 32MB cap):
   - viewport tile 数 ≤ 128: 195 MB, 视觉正常
   - viewport tile 数 > 128 (大屏 / 大 zoom): 模糊 (cap 装不下, 自相残杀)
build 116 final (按 viewport 严格管): 80-150 MB, 视觉正常
```

跟用户记忆"以前 80MB" 对齐. zoom out 跨 level 重解码 viewport tile 时
单 tile ~0.38ms × 4 worker 并发 ~10ms 不可感知.

(GuoheView L47)

## 1.6.0 — build 115

### Fixes — Reactive binding mount-phase 抑制动画 (Widget 基类机制)

- 修 `Toggle` / `CheckBox` / `RadioButton` / `ProgressBar` 在 mount 阶段的"先默认值后真实值"动画闪烁: caller 走 `ui_page_prepare_window` + `ui_page_set_bool` + `ui_window_show_immediate` 标准 pattern 喂初值时, 之前 widget 仍会播一段 `.uix data()` 默认值 → caller 真实值的 transition (toggle 滑动 / checkbox 勾选缩放 / progress 缓动). 用户看到的就是"打开窗瞬间 switch 从 ON 滑到 OFF"这种伪闪烁.

  根因: `ApplyBindingToWidget` 用各 widget 的 `boundOnce_` 判断"第一次 binding push 走 snap, 之后走动画". 但 `ui_page_load_string` 内部 `CompileAndAttachPage` 注册 `WatchEffect` 时立即 fire 一次, 用 `.uix data()` 默认值消耗了 snap 通道; caller 后续 `ui_page_set_bool` 推真实值反而走动画路径 → 触发 transition.

  修法 (Widget 基类机制, 一次到位):
  - `Widget` 基类加 `paintedOnce_` 字段 + `PaintedOnce()` / `MarkPainted()` 接口
  - `Widget::DrawTree` 及 5 个子类 override (TabControl / ScrollView / Expander / SplitView / StackWidget) 各自在 `OnDraw(r)` 后立即设 `paintedOnce_ = true`
  - `PageState::ApplyBindingToWidget` 把 `SetXxxFromBinding` 路径改成 `widget->PaintedOnce() ? SetXxx(v) : SetXxxImmediate(v)` ternary — 用 widget 自身的"画过没"作 gate, 跟 boundOnce_ "推过几次"解耦
  - 删 4 widget 的 `boundOnce_` 字段和 `SetXxxFromBinding` 方法 (功能已被 PageState 端 ternary 取代); `SetXxxImmediate` 内部不再设 `boundOnce_=true`
  - 顺手补 `SliderWidget` (它原本漏了 FromBinding 通道, 但实测 SetValue 不带 value 动画, 不需要 ternary, 注释说明)

  语义对齐用户期望: widget 第一次被画到 RT 之前所有 reactive binding push 都 snap (mount 阶段), 第一次 OnDraw 之后才动画 (运行时切换). caller 调 `ui_page_set_bool` 在 prepare 之前 / 之间任意次数都不闪. v-if 销毁重建 widget 实例 `paintedOnce_` 自然重置为 false, 重新显示瞬间也 snap. CSS transition 路径独立未动 (本次只修 reactive binding 这条管道). (GuoheView L45)

## 1.6.0 — build 114

### Theme

- 新增 `--bg-elevated` (`Colors::elevatedBg`) token, 设计为 sidebar+content 类布局中介于 `windowBg` 和 `sidebarBg` 之间的"夹层"背景色: 浅 `#fcfcfc` / 深 `#242424`. 用于 dialog/settings 类页面让 content 区跟 chrome (sidebar) 视觉分层但不撞色, 不需要 caller 自己 hardcode hex 切深浅. grey 命名空间补 `g99` (#fcfcfc), 跟现有 g96/g98 同段细分. (GuoheView L44)

## 1.6.0 — build 113

### Theme

- `titleBarBg` 跟 `windowBg` 视觉拉齐：浅色 `#fafafa` → `#ffffff`，深色 `#1f1f1f` → `#292929`。`<TitleBar>` 默认背景现在跟 shell/canvas 同色，frameless 应用一体感更强；之前 Fluent2 给 titleBar 分配独立 `neutralBackground2` token 在 frameless 模式下会形成明显色带。需要保留原视觉的应用可继续用 `ui_titlebar_set_bg_color` 显式覆盖。（GuoheView L43）

## 1.6.0 — build 112

**Minor 版本号上调 (1.5.0 → 1.6.0)** —— 整合 1.5.0 公开发布 (build 46) 以来 65 笔 build 的能力扩展和 bug 修复。这一节是面向公开发布的总结，详细 per-build 历史保留在下面的 build 47–111 各段。

### 🆕 新增能力 — Widget 级事件回调一整套

之前只能给 `button` / `input` 这类预制 widget 挂 onclick。1.6.0 开放任意 widget（含自绘 `<custom>`）的事件回调：

- `ui_widget_on_mouse_move` — 鼠标位置（widget-local 坐标），build 60
- `ui_widget_on_mouse_leave` — 离开 widget 矩形，build 61
- `ui_widget_on_mouse_wheel` — 通用滚轮回调，不再只能在 ScrollView 拿，build 66
- `ui_widget_on_focus` / `ui_widget_on_blur` — 配合 `ui_custom_set_focusable` 让自绘 widget 进键盘焦点系统，build 64
- `ui_widget_set_cursor` — 程序化光标 (`IDC_HAND` / `IDC_WAIT` / `IDC_SIZE*` 等)，build 58
- `ui_icon_button` 加 ghost 模式 hover 视觉开关，build 57

### 🆕 新增能力 — Menu 反应式重构

老的 `WireMenus` 静态简化路径**已删 (BREAKING)**。新的 `<menu>` 全走 Vue 3 风格：

- `<menuitem v-for="x in list" :key="x.id">` 动态列表
- `v-if="cond"` 条件显示
- `:disabled="!ready"` 双向 binding
- 嵌套 submenu 同款语法
- rclick dispatch 改 **deepest-match** (build 107-108)：嵌套 `<menu trigger>` 时子 widget 的 menu 不再被祖先抢走，跟 DOM event bubbling 语义一致
- submenu autoclose 命中改"可见矩形" (build 87)，箭头改 outline，宽度跟主菜单一致 (build 78-85)

### 🆕 新增能力 — 窗口生命周期 / 首帧体验

- `ui_page_prepare_window` 隐藏窗 + RT 预创建路径，首帧零闪、零黑屏，build 99
- `UiWindowConfig.start_maximized` hint，最大化启动无 "normal → max" 1 帧闪，build 105
- `UiWindowConfig::owner` 子窗口附属主窗口（任务栏不独立 / 跟随主窗 minimize），build 65
- `ui_debug_server` 支持多 server 并存，每窗口独立 pipe，build 63
- `ui_window_dpi_scale` 通用 API，build 101
- `ui_page_prepare_window` 配合 `start_maximized` 实现持久化"最大化关→最大化开"零闪烁

### 🆕 其它新增

- `:id="..."` 动态绑定，v-for 唯一 id 终于能用，build 98
- `ui_toast_ex(anim)` 新加 `UI_TOAST_ANIM_FADE` 纯渐入渐出风格，build 106
- Toast 内部动画切到 time-based，解 WM_TIMER 合并 tick 时掉帧，build 68

### 🐛 Bug 修复合集 — 用户实战反馈驱动

ScrollView 三连修 (builds 109-111)：
- 109 `<ScrollView>` 包 v-if/v-for panel 时 layout 错乱 → 总是包 wrapper VBox + patch `parentWidget`
- 110 ScrollView 的 CSS `padding` 之前 vainly → DoLayout 扣 padL/padT/padR/padB
- 111 frameless 窗底边拖不动 → NC hit-test 跟左右顶一致 (始终返 HTBOTTOM)

其它关键修复：
- build 95 WM_KEYDOWN onKey 自销毁路径 fall-through UAF
- build 96 `PushRoundedClip` 加 `INITIALIZE_FOR_CLEARTYPE` 防 layer 内 ClearType 文字消失
- build 90 SVG presentation attrs (`stroke` / `fill` / `stroke-width`) 父→子继承
- build 92 LabelWidget 接 CSS `line-height`（默认 1.3 × font-size）
- build 93 LabelWidget `white-space: nowrap` 接通
- build 91 Toggle ON 态 thumb 暗色模式去黑改白（之前看不见）
- build 62 LabelWidget `SizeHint` 返自然 intrinsic 宽 → 修 shrink-to-fit 父级死锁

### 💥 BREAKING（升级时必读）

| build | 改动 | 迁移 |
|---|---|---|
| 75 | menuitem 重设计成 widget 模板 | `<menuitem>X</menuitem>` 改成 `<menuitem><label>X</label></menuitem>` |
| 94 | 窗口几何 API x/y 全改 screen px（之前 DIP 不一致） | 配合 `ui_window_dpi_scale` 转换 |
| 106 | `ui_toast_ex` 新增 `anim` 参数；删 `ui_toast_at` | 旧调用末位补 `UI_TOAST_ANIM_SLIDE` |
| 73 | `WireMenus` 静态简化路径已删 | menu 必须走 `<menu>` 声明式 / Vue 3 binding |

---

## 1.5.0 — build 111

### fix(window): 底边 NC hit-test 跟其他三边一致 (L29 follow-up)

GuoheView 反馈 settings 窗底边拖不动 + 主窗底边也拖不动. 根因不是
GuoheView 没设 resizable, 而是 lib OnNcHitTest 底边 hit-test 跟其他
三边逻辑**不一致**:
  - top/left/right: 始终返 HTTOP/HTLEFT/HTRIGHT
  - bottom: 先 HitTest widget, 命中就 HTCLIENT 让给 widget

主窗底部有 toolbar (cmd buttons), settings 底部有 ScrollView, HitTest
几乎必然命中 widget → 永远 HTCLIENT → 用户拖不到 frame resize.

**修复**: `if (b) return HTBOTTOM;` 跟左右顶同款. kResizeBorder ~5-8px
极小一块, 让给 resize 不影响 widget 主体交互区 (button 通常 36px+).

## 1.5.0 — build 110

### fix(scrollview): DoLayout 应用 padding (L29 follow-up)

GuoheView 反馈: settings 窗 `.content` CSS `padding: 24px 28px`, 改用
`<ScrollView class="content">` 包后内容贴 ScrollView 边, padding 完全没视觉效果.

**根因**: `ScrollView::DoLayout` (controls.cpp:2082) 直接用 sv.rect 算
visW/visH/content.rect, 忽略 Widget 基类的 padL/padT/padR/padB 字段.

**修复**: visW/visH 扣 padding, content 起点偏 padL/padT. 跟 VBox/HBox
等 flex container 的 padding 语义一致.

**行为变化**: 单 padding=0 的 ScrollView 跟之前 layout 完全一致. 之前
caller 设了 padding 但视觉 vainly — 现在视觉对了, 属于"修对了没破坏"
类型, 不算 breaking.

## 1.5.0 — build 109

### fix(compiler): ScrollView 总是包 wrapper + patch v-if/v-for parentWidget (L29)

GuoheView 反馈: `<ScrollView>` 包 多个 v-if panel 后, 缩窗口想看 ScrollView
按需出滚动条, 实测 panel 区域空白 / panel 互相抢 viewport.

**根因**: compiler ScrollView post-process (compiler.cpp:1136-1148) 跟 lib
v-if mount runtime 数据流冲突.
  - compiler 处理 `<div v-if>` 时把 `CompiledConditional.parentWidget` 记成
    当前 widget (= ScrollView sv, line 220).
  - compiler 处理 ScrollView 自己时, 分 size==1 (SetContent(only)) /
    size>1 (包 wrapper) 两条路.
  - runtime mount 时 `parentWidget->InsertChild(...)` (page_state.cpp:1380),
    parentWidget 还是 sv, panel 插到 sv.children 而非 wrapper.
  - ScrollView::DoLayout 假设 single content_, 但 sv.children 实际包含
    wrapper + N 个 mounted panel, layout 错乱.

**修复**: ScrollView post-process **总是**包 wrapper VBox (不论 kids count),
同时遍历 `ctx.out->conditionals` / `ctx.out->loops` 把 `parentWidget == sv`
的项改成 `parentWidget = wrapper`. 运行时 v-if/v-for mount InsertChild 到
wrapper, sv.children 永远只有 wrapper 一个, ScrollView layout 假设成立.

**行为变化** (单 child ScrollView):
- 之前 only widget 直接是 sv child, 现在多一层 VBox wrapper.
- VBoxWidget 默认无 margin/padding/border/bg, 视觉无差.
- CPU 多一次 DoLayout, 可忽略.

**ABI**: 不破坏. SetContent 是 lib 内部 API, 外部 caller 通过 `<ScrollView>`
标签使用, 不直接调.

回归面: core-ui demo/examples 都没用 `<ScrollView>`, 仅 GuoheView 一处.

## 1.5.0 — build 108

### fix(menu): AttachWindow 同款 deepest-match (L28 完整 fix)

build 107 漏修一处: `PageState::AttachWindow` (page_state.cpp:973) 在
`WireSubtreeMenus` 之后被调用, 重新设置 `win->onRightClick` lambda
**覆盖**了 WireSubtreeMenus 写的版本. 之前只改了 WireSubtreeMenus 那处
first-match→deepest-match, AttachWindow 这处仍是 first-match — 实际生效
的还是父 trigger 抢先匹配, 跟 build 106 行为一致.

GuoheView 测试发现: 加 `<menu trigger="#minimap" event="rclick">` 后,
升 build 107, 右键 minimap 仍弹父 canvas_body 菜单. 临时 diag patch
fprintf 到 `%TEMP%\core_ui_rclick_diag.log` 看到 `WireSubtreeMenus`
注册了 2 个 menu (canvas_body + minimap), 但 WireSubtreeMenus 那处
onRightClick lambda 完全没被调用 — 被 AttachWindow 覆盖了.

修复: AttachWindow line 973 onRightClick lambda 同款 deepest-match.

build 107 已 ship 但实际是 partial fix — caller 看 build 107 不工作不
应该回退 (会丢这次的 PageState 全套), 应直接升 108.

未来重构建议: 两处 onRightClick wire 重复, 抽 helper. 留 build 109+.

## 1.5.0 — build 107

### fix(menu): rclick dispatch first-match → deepest-match (L28)

GuoheView 反馈: 给 minimap (鸟瞰图) widget 加 `<menu trigger="#minimap"
event="rclick">` 后, 右键 minimap 仍弹父 canvas_body 的菜单, 子 widget
自己的 menu 不出现.

**根因**: `WireSubtreeMenus` 的 `onRightClick` lambda 按 rclickList 声明
顺序线性匹配 (`page_state.cpp:601`):
```cpp
for (const auto& t : rclickList)
    if (t.triggerElement->Contains(x, y)) { ShowMenu(t.menu); return; }
```
.uix 通常 `<menu trigger="#父">` 写在 `<menu trigger="#子">` 之前 (跟着
widget tree top-down 自然顺序). 父 trigger 先注册, 父 `Contains(x, y)`
对子区域也返 true, 抢先 ShowMenu, 子 widget trigger 没机会.

**修复**: rclick dispatch 改 **deepest-match** — 遍历找 `Contains(x, y)`
里 widget tree 深度最深的 trigger. 跟 DOM event bubbling 语义一致, 子
widget rclick 总是拿到自己的 menu. 算法 O(N × depth), N 通常 < 10.

**不破坏现有 caller**:
- 单 trigger 场景跟 first-match 一致
- 多 trigger **互不嵌套** (不同 widget tree 分支) 跟 first-match 一致 (同
  时只有一个 Contains 命中)
- 仅嵌套 trigger 场景行为变化: 子 trigger 现在能拿到 rclick, 这本来就是
  caller 写 `<menu trigger="#子">` 的预期

GuoheView 场景: rclick minimap 弹 minimap menu, rclick image_host /
canvas 空白处弹 canvas_body menu.

## 1.5.0 — build 106

### feat(toast): UI_TOAST_ANIM_FADE — 纯渐入渐出, y 全程不变 (L26)

旧 toast 只有 slide 风格. center 位置 slide 没视觉效果 (hideOffset=0)
等于"啪"地直现, 不够 polished. 加 fade 风格给 caller 可选, 适合
复制成功 / 设置已应用这类不应抢视线的轻量提示, 也补齐 center 位置
进入动画.

API 改动 (**BREAKING**):

- 新加 enum:
  ```c
  enum {
      UI_TOAST_ANIM_SLIDE = 0,   /* 默认: top/bottom 滑入, center 直现, 退出淡出 */
      UI_TOAST_ANIM_FADE  = 1,   /* 纯渐入渐出: alpha 0→1→1→0, y 全程钉在目标位置 */
  };
  ```
- `ui_toast_ex` 签名增加 `anim` 参数:
  ```c
  // 旧 (build 105 及之前):
  void ui_toast_ex(UiWindow win, const wchar_t* text, int duration_ms,
                   int position, int icon);
  // 新 (build 106+):
  void ui_toast_ex(UiWindow win, const wchar_t* text, int duration_ms,
                   int position, int icon, int anim);
  ```
- 删 `ui_toast_at` — 用 `ui_toast_ex(win, text, dur, pos, 0, UI_TOAST_ANIM_SLIDE)` 替代
- `ui_toast` (3 参数) 完全不变

迁移指南:

| 旧调用 | 新调用 |
|---|---|
| `ui_toast(win, L"X", 2000)` | 不变 |
| `ui_toast_at(win, L"X", 2000, 1)` | `ui_toast_ex(win, L"X", 2000, 1, 0, UI_TOAST_ANIM_SLIDE)` |
| `ui_toast_ex(win, L"X", 2000, 0, 1)` | `ui_toast_ex(win, L"X", 2000, 0, 1, UI_TOAST_ANIM_SLIDE)` |
| (新增) | `ui_toast_ex(win, L"X", 2000, 1, 0, UI_TOAST_ANIM_FADE)` — 纯渐隐 |

渲染实现: WM_TIMER tick handler 完全不动, `toastPhase_` / `toastSlide_`
推进语义不变. `DrawToast` 分两路解释进度:

- `SLIDE`: y 用 hideOffset 插值, alpha 仅 phase3 衰减 (旧行为)
- `FADE`:  y 全程钉在 targetY, alpha = (phase1|phase3) ? slide : 1.0

顺手加 `if (alpha < 1/255) return;` 提前剔除, 防 phase 切换瞬间残留
1 帧 alpha=0 闪 + 省 GPU FillRoundedRect cost.

cost: 内部新增 `int toastAnim_` 一个字段 (4 字节). 周期总时长不变
(slide-in 200ms + hold + slide-out 250ms), `duration_ms` 参数语义跟旧
版本一致, caller 无需调整.

## 1.5.0 — build 105

### `UiWindowConfig.start_maximized` hint — 最大化首帧无闪 (L25)

新增 `int start_maximized` 字段（结构体末尾，零初始化保持现有 normal 行为
— **非 BREAKING**）。设 1 时 lib 内部走现成的 preMaximized 路径
(`Show()` / `ShowImmediate()` 都识别), 首帧直接 maximized, 没有
"normal → max" 1 帧闪烁.

motivation: caller 持久化 "最大化关→最大化开" 需求, 之前只能在 Create
后自己调 `ShowWindow(SW_MAXIMIZE)`, 跟 lib 内部 `SW_HIDE` + layered
fade-in 时序撞会出 1 帧 normal flash. hint 把这事统一在 lib 一侧.

具体改动:
- `UiWindowConfig` 末尾加 `int start_maximized`
- `UiWindowImpl::startMaximizedPending_` (public, 跟 `skipOpenAnimation_`
  同款 lib 内部 flag), `ui_window_create` 一次拷
- `Show()`: `preMaximized = IsZoomed(hwnd_) || startMaximizedPending_`,
  消费一次清零 (避免 ShowWindow(SW_HIDE)+Show() 循环里残留)
- `ShowImmediate()`: 末尾 `ShowWindow(SW_SHOWMAXIMIZED if start_max else
  SW_SHOWNA)`

GuoheView (主消费者) 接通: `application.cpp` on_close 用
`GetWindowPlacement` 拿 showCmd + rcNormalPosition, save maximized 标志
+ 还原态 rect; restore 时填进 wcfg.start_maximized.

## 1.5.0 — build 104

### feat(renderer) + fix(gh_img_view): SetPreview / SetTile 接收 straight alpha — 修透明 PNG 大倍率 chroma fringe (L19)

GuoheView L17 暴露的契约不匹配: lib API 不声明 alpha mode, caller 默认
PNG/JPEG 解码器输出 straight (alpha=128 时 RGB 保留原值, e.g.
(255,255,255,128)), 但 lib 内部 CreateBitmapFromPixels 写死 D2D
PREMULTIPLIED 模式 (期望 RGB 已乘 alpha, e.g. (128,128,128,128)). D2D
HQ_CUBIC 上采时按 premul 公式插值 straight 数据 → 颜色贡献被双倍计算 +
sRGB gamma 三通道非对称 → R/G/B 失衡 → 抗锯齿边缘 chroma fringe (透明
PNG 大倍率放大尤其明显).

修复 (默认行为破坏性变动, 跟项目"lib 破坏性变动允许"红线对齐):
  - Renderer: 加 CreateBitmapFromPixelsStraight() — 内部 RGB *= A/255 转
    premul 再走原 CreateBitmapFromPixels 路径. 适合直接喂 PNG/JPEG 解码
    器输出 (ghde / WIC GUID_WICPixelFormat32bppBGRA / libpng / stb_image
    等几乎所有解码器默认行为).
  - GhImgViewWidget: SetPreview / SetTile 改调 ...Straight 版本.
    输入约定语义化: caller 现在喂 straight 数据 (跟解码器对齐), lib 自己
    处理 alpha 模式. caller 不用再做手动 premul.

BREAKING (跟旧契约不兼容):
  - 之前没明示但实际 caller 应该喂 premul (D2D bitmap PREMULTIPLIED 模式
    要求). 实际外部 caller 几乎都没意识到这条 — 喂的都是 straight, 大倍率
    放大才暴露 fringe.
  - 现在 lib 把 straight 当主流, 内部 premul. 如果 caller 已经 premul 过
    (跟过去 D2D 文档对齐的"对的"用法), 会被 lib 再 premul 一遍, 颜色变错.
  - 解法: caller 不要预先 premul 了, 喂原始 BGRA 给 lib.

性能代价: 每次 SetPreview / SetTile 多一次 width×height 的 BGRA 遍历
(uint8 算术, 无浮点). 80MB image ~10-30ms 量级, 在"加载图片"路径上 caller
感知不到. tile path 通常 256×256 = 0.25MB, <1ms.

新增 API: Renderer::CreateBitmapFromPixelsStraight(pixels, w, h, stride) —
跟 CreateBitmapFromPixels 同签名, 只是先 premul 再 upload. 外部 caller
可以按需用; SetPreview / SetTile 默认走它.

## 1.5.0 — build 103

### fix(gh_img_view): 大幅下采样自适应 LINEAR — 消高对比边色边振铃 (L14)

用户复现: 3196×6287 长图 fit zoom 0.12 (~8× 下采), 图像中暗色 banner →
白色画布的边缘出现明显蓝紫色色边. 不只是这张图 — 任何 zoom < 0.5 的
PNG/JPG 都会撞.

根因:
  - D2D HIGH_QUALITY_CUBIC 采 4×4 邻域 + Catmull-Rom 负权 lobe
  - 8× 下采时 4×4 = 16 邻域只覆盖源 8×8=64 像素的 25%, 信息严重欠采
  - 高对比边缘 (灰/黑 → 白) 上 negative-lobe overshoot, R/G/B 三通道
    overshoot 幅度因源像素亮度不同而不同 → 灰银色的微弱蓝偏被放大成
    可见蓝紫边
  - D2D 文档明示 HQ_CUBIC 适用范围 0.5×~2×, 之外行为未定义

修复 (gh_img_view.cpp anonymous namespace + 两处 draw site):
  - 加 PickInterp(screen_per_source) helper: < 0.5 → LINEAR (D2D 没有
    HQ_LINEAR 常量, 直接用 LINEAR 即 2×2 bilinear), 否则 HQ_CUBIC
  - OnDraw 的 preview_ 兜底 draw 跟 DrawLevel 的 tile draw 都接 picker
  - preview 路径用 dest_w / preview_pixel_w 算 scale (本来内部就有 dest,
    GetPixelSize 拿 preview 实际像素宽度)
  - tile 路径 scale 局部变量已是 screen-per-source, 直接传 picker

为什么 LINEAR 在大幅下采下视觉上不输 CUBIC:
  - 大幅下采本来就在丢高频细节 — CUBIC 的"锐"是 negative lobe 在 ring
    出来的伪锐, 不是真细节
  - LINEAR 是 2×2 bilinear, 无负权 → 永不 ring, 边缘干净
  - 4× 以上下采两者锐度差极小, 但 LINEAR 没色边

影响面:
  - fast path 全图 BGRA preview (GuoheView 长图.png 类) — 立刻消色边
  - slow path tile pyramid 在 active_level 选错偏下时 — 同样受益
  - 不影响 1:1 / 上采 (CUBIC 路径继续生效)

## 1.5.0 — build 102

### fix(gh_img_view): PickAutoLevel 感知 DPI, 避免上采样 — 修默认 fit 模糊 (L10)

用户原话: 默认糊, 放大缩小后清晰. 之前 L7 (build 100 阈值 1.0→0.5) +
L8 (HQ_CUBIC) 部分缓解但没根治, 因为 PickAutoLevel 没感知 DPI scale,
用 DIP 单位的 zoom 算阈值. DPI 150% 下永远选偏上采样的 level.

实例 (世界地图 9934px fit zoom=0.108, DPI 150% 屏物理 1610px):
  L2 (2483px) 渲染到 1610px = 下采样 1.5x (锐利, 应该选)
  L3 (1241px) 渲染到 1610px = 上采样 1.3x (模糊, 旧算法选这个)

机制:
  - widget rect / zoom 都是 DIP 单位
  - D2D RT.SetDpi(144) 让 D2D 内部把 DIP * 1.5 → 物理像素渲染
  - 旧 PickAutoLevel: screen_per_level = LevelToScreenScale * zoom, 缺
    *dpi_scale, 算到的是 "DIP_per_level_pixel" 而非真正的"屏幕物理像素
    密度"
  - 阈值 0.5 (DIP) 在 1.5x DPI 下转物理是 0.75, 仍偏向选上采样 level

为什么 "放大缩小后清晰":
  1. wheel-in zoom 0.108 → 0.27, PickAutoLevel 切到 L1 (4967px)
  2. push_visible_tiles_ 入队 L1 visible tile, worker 解出来 set_tile 推上
     widget tiles_ 缓存
  3. wheel-out 回 zoom 0.108, active_level 切回 L3, 但 L1 tile 还在 cache
  4. OnDraw 从最粗 level 画到最细, L1 后画覆盖 L3 → 屏上是 L1 (4967px →
     1610px 物理) 下采样 3x 渲染 = 锐利
  5. 这就是用户看到的"放大缩小后清晰"

新算法:
  uint32_t result = 0;
  for (lvl = 0; lvl < levels; ++lvl) {
      scale_phys = LevelToScreenScale(lvl) * zoom_ * dpi_scale_;
      if (scale_phys > 1.0f) break;
      result = lvl;
  }
  return result;

物理含义: 选满足 "level 像素跨 ≤ 1 屏幕物理像素" (= 下采样 ≥ 1.0x) 的
最大 lvl. 永不上采样. 默认 fit 直接选到 wheel-cycle 后才能拿到的下采样
level, 不再需要用户操作.

widget 加 dpi_scale_ 成员 (默认 1.0), Begin 时从 Renderer.RT()->GetDpi
读 + cache. caller 不需要显式喂 — Begin 自动同步当前 RT 的 DPI.

## 1.5.0 — build 101

### fix(gh_img_view): OnDraw 用 HIGH_QUALITY_CUBIC, 修 fit 下采样模糊 (L8a)

OnDraw / DrawLevel 一直用 `D2D1_BITMAP_INTERPOLATION_MODE_LINEAR` (D2D 1.0
旧 API). 据 MS Learn / GameDev.net 文档, **LINEAR 模式只对上采样做插值,
下采样时不做低通滤波** — zoom<1 时直接 nearest 取样, 产生 alias 噪点 +
锐度损失. 用户体感: 打开大图首次 fit 糊, 放大缩小后清晰 (caller wheel-in
push 了更高 level tile 进 cache, OnDraw 从粗到细覆盖渲染, 高 level tile
缩小比例更小 alias 轻微).

Build 52 + 100 调过 PickAutoLevel 阈值 (1.0 → 0.5), 缓解但没根治, 因为
LINEAR 本身就不滤波. 这版换成 D2D 1.1 API `DrawBitmapHQ` +
`D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC` — 三次 Lanczos 类插值,
自带 mipmap LOD, 下采样有低通滤波. PS / Photos / IrfanView 等专业图像
查看器都用 cubic 或 anisotropic, 不用 LINEAR.

性能: visible tile ~10-100 (一屏), HIGH_QUALITY_CUBIC 比 LINEAR 慢 ~3x
但 GPU 加速下绝对 ms 级. 实测:
- world 9934×7016 L3 fit 锐度 (Laplacian variance): 488 → 1900+
- world 系统照片查看器显示 (用 HQ_CUBIC 路径): ~2070

### feat(window): `ui_window_dpi_scale(win)` 通用 API (L8b)

新 API 返当前显示器 DPI / 96 比例 (100%→1.0, 125%→1.25, 150%→1.5).

需求: PMv2 + SetDpi(144) 环境下, `ui_gh_img_view_set_zoom(1.0)` 是
"image pixel : DIP = 1:1", 实际渲染到屏被 D2D 内部 *1.5 → 看上去图大
150%. 真正的 "1:1 image vs screen physical" 需要 zoom = 1.0 / dpi_scale.

caller 用法:
```cpp
float scale = ui_window_dpi_scale(window);
ui_gh_img_view_set_zoom_around(canvas, 1.0f / scale, ax, ay);
```

通用性: 任何要 "1:1 / 物理像素对齐" 语义的 widget caller 都能用. 不局限
gh_img_view.

## 1.5.0 — build 100

### fix(gh_img_view): PickAutoLevel 阈值 1.0 → 0.5 — 修 fit 大图模糊 (L7)

旧算法选 `screen_per_level_px >= 1.0` 的最低 level. zoom < 1 (fit 大图)
时挑出 scale 略 > 1.0 的级, 实际是 D2D LINEAR **上采样** 1.x 到屏 —
肉眼可见模糊渐变. 用户体感: 打开大图首次 fit 糊, 放大缩小后 (caller
wheel-in 时 push 了更高 level tile 到 widget cache, OnDraw 从最粗到最细
全画, 高 level tile 后画覆盖) 反而清晰.

实例 (18641² TIFF, zoom=0.041):
- L4 scale=0.656, L5 scale=1.31, L6 scale=2.62
- 旧算法 (≥ 1.0): 选 L5 → 上采样 1.31x, 模糊
- 新算法 (≥ 0.5): L4 scale=0.66 ≥ 0.5 → 选 L4, 下采样 1.5x, 锐利

D2D LINEAR 下采样保留高频信息 (相邻 pixel 平均), 上采样产生模糊 (相邻
pixel 之间线性插值). PS / Lightroom 在 zoom<1 时同样优先用比目标 zoom
略高的 mip 下采样.

阈值 0.5 的物理含义: 屏幕 1 像素跨 ≤ 2 个 level 像素, 即下采样 ≤ 2x.
兜底分支 (所有 level scale < 0.5) 仍返 levels-1, 保留极小 zoom 兜底.
无新 API, 算法内常量调整.

## 1.5.0 — build 99

### feat(page): `ui_page_prepare_window` — 隐藏窗 + RT 预创建路径 (L27)

`ui_page_open_window` 内嵌 create+set_root+attach+show 四连, 应用没法在
窗已建好 / reactive 已挂 / D2D RT 已准备但**还没 ShowWindow** 的状态做
同步预热 (隐藏 hint 面板 / 同步解码 argv 图片 / 喂 bitmap). 用户 argv
启动 (双击文件关联) 看到"窗口先出现, 0.3-1s 后图才出现"两段式卡顿.

新增 API `ui_page_prepare_window(UiPage, UiWindowConfig*) → UiWindow`,
跟 open_window 同走 create + set_root + AttachWindow, 但**不**调 show;
末尾改成 `ui_window_prepare_rt` 把 D2D RT 提前建好. Caller 完成预热
后调 `ui_window_show_immediate(win)` 一次性出图.

用法对照:
```cpp
// 普通启动 (保留 fade-in 动画)
win = ui_page_open_window(page, &cfg);

// argv 启动场景 (无 fade, 首帧就是图)
win = ui_page_prepare_window(page, &cfg);   // 窗 hidden, RT ready
load_image_sync(file);                        // 同步解码 + 喂 widget
ui_window_show_immediate(win);                // 一次性出图
```

实现: 提取共用 page_open_or_prepare_ helper, show=true 调 ui_window_show,
show=false 调 ui_window_prepare_rt. open_window / prepare_window 都封装它.
原 open_window 行为零变化 (调用方不感知).

## 1.5.0 — build 98

### feat(binding): `:id="..."` 动态绑定支持 — v-for 唯一 id (L26)

`ApplyBindingToWidget` 之前只处理 text / class / visible / opacity / color
/ enabled 等 prop, 没有 "id" 分支 → `:id="..."` 绑定的 attr 求值后被静默
丢弃, widget 的 id 一直空, `ui_page_on_widget_mount` 按 id 注册的回调
永远不会触发.

典型用例: v-for 给每个 iteration 唯一 id 挂 mount hook 收集 widget handle.

```uix
<!-- 之前: cb 都拿不到 id, mount hook 不 fire -->
<input type="checkbox" v-for="item in items" :id="`cb_${item.key}`"/>

<!-- 之后: build 98+ 每个 item 拿到 cb_xxx id, mount hook 按 id 触发 -->
```

实现 1 个 if 块:

```cpp
if (prop == "id") {
    w->id = v.ToString();
    return;
}
```

调用时机链路 (v-for iteration build):
  1. 创建 widget (id 空)
  2. 注册 bindings 作为 watch effect, 首次 eval → ApplyBindingToWidget(id=...) → 设 w->id
  3. DispatchMountHooks 走 tree 命中已注册的 hook (按 w->id 索引)

GuoheView 主用例: 设置 → 文件关联面板 用 v-for 渲染 ghde 支持的扩展名
checkbox, 每个 cb_<ext> 装独立的 onChange handler 收用户操作.

### 已知限制

- `w->id` 中途变化只更新值, 不联动 unmount/mount lifecycle (要求 id stable
  across re-render — v-for 的 stable identity 用例够用; 想做"id 改名自动
  重新挂 hook"的场景不支持, 用 v-if 包整体卸载重建).
- :class / :style 等其他 attr 的处理路径不动, 现有功能无回归.

## 1.5.0 — build 97

### chore(version): VERSIONINFO 加 LegalCopyright + CompanyName 改 ghxi

之前 PE VERSIONINFO 缺 LegalCopyright (Explorer 属性页"详细信息"标签
里"版权"栏空), CompanyName 是 "Core UI" (跟 ProductName 重复). 改:

  CompanyName    "Core UI" → "ghxi"
  LegalCopyright (无)       → "ghxi"

ProductName / FileDescription / FileVersion / InternalName /
OriginalFilename / ProductVersion 不变. LangID 0x0804 (zh-CN) +
CodePage 1200 不变.

## 1.5.0 — build 96

### fix(renderer): PushRoundedClip 加 INITIALIZE_FOR_CLEARTYPE 防 layer 内 ClearType 文字消失 (L25)

`Renderer::PushRoundedClip` 创建 D2D1 layer 用 `D2D1_LAYER_OPTIONS_NONE`,
其后 layer 内的 DrawText 在 CLEARTYPE 渲染模式 (lib build 92+ 默认, 用户
通过 `ui_theme_set_text_render_mode(UI_TEXT_RENDER_CLEARTYPE)` 调成 Office
/ Chrome 风) 下 sub-pixel 合成无法正确进行 — 文字几乎不可见.

典型表现:
  - ComboBox 弹出 popup 里的 item 文字白底白字看不见
    (`ComboBoxWidget::OnDrawOverlay` 先 PushRoundedClip 再画 items 文字)
  - 任何其他用 PushRoundedClip 包裹文本的场景同款

为什么闭合状态的 ComboBox text 正常显示: OnDraw 路径不进 layer, ClearType
直接画到 RT 上, sub-pixel blend 不受影响.

修法 1 行: PushLayer 的 LayerParameters 从 `D2D1_LAYER_OPTIONS_NONE` 改
`D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE`. 这个 flag 告诉 D2D layer
backing 已初始化为不透明色, ClearType 可以做正确的 sub-pixel blend.

副作用: 略多一点初始化开销 (layer 在 push 时清成 cleartype-ready 状态),
肉眼无感; 兼容所有窗口表面 (不依赖 BitmapRenderTarget). 不影响 GRAYSCALE
渲染模式 (D2D 那条路径不依赖 layer flag).

## 1.5.0 — build 95

### fix(window): WM_KEYDOWN onKey 自销毁路径不再 fall-through UAF (L24)

`HandleMessage` 里 `case WM_KEYDOWN` 之前用 `break;` 收尾, 之后所有 case
落到末尾的 `return DefWindowProcW(hwnd_, msg, wParam, lParam)`. 这条路
正常情况无害, 但若 `onKey` 回调内销毁本窗口 (典型: 子窗装 on_key,
里面调 `ui_page_destroy` 关自己), 控制权返回 WndProc 时 `this` 已被释
放, `hwnd_` 字段悬空, DefWindowProcW 解引用 → 崩.

GuoheView 实测复现: 信息子窗装了 Tab 关窗回调, 用户连按两下 Tab 第二
下时焦点在子窗, 子窗 onKey 销毁自身, lib fall-through UAF.

修法 1 行: WM_KEYDOWN case 末尾 `break;` → `return 0;`. 既然装了 onKey
就把键事件视为完全消费, 不再下传 DefWindowProc.

副作用:
  - DefWindowProc 对 WM_KEYDOWN 的默认处理主要是 F10 / Alt 触发系统菜
    单 (走 WM_SYSCOMMAND), 但 lib 应用无 system menu, 不依赖
  - Alt+F4 / Alt+Space / F10 等系统快捷键走 WM_SYSKEYDOWN, 跟此分支无关
  - 没装 onKey 的窗口 `if (onKey)` false, 跳过整个块, 不受影响

## 1.5.0 — build 94

### **BREAKING**: 窗口几何 API x/y 全 screen px, 跟 setter 自洽 (L23)

之前同一组"窗口几何 API"里 getter / Create 的 x/y 是 DIP, setter
(set_position / set_rect) 的 x/y 是 screen px, 不自洽 — 用 get→set
把窗口几何复制到 sub-window 在非 100% DPI 下必偏移. 原因是 build 76+
L6 fix 想解决"持久化 x/y 跨 DPI 漂移"把 DIP 静默隐藏在 getter 里, 但
setter 一直是 screen px 没跟着改, 半路改完了一边.

本版改齐: x/y 统一 screen px (Win32 惯例, 跟 SetWindowPos 一致); w/h
保持 DIP. round-trip `get → set` / `get → Create` 任意组合都成立.

具体影响:
- `ui_window_get_rect_screen` 返 r.left / r.top 原值 (screen px), 不
  再除 dpiScale_
- `UiWindowConfig.x/y` 传 screen px, 不再 MulDiv. `x=0 && y=0` 仍是
  "屏幕居中" 特殊值
- `ui_window_set_position` / `ui_window_set_rect` 行为不变 (一直是
  screen px)
- `ui_window_set_size` 内部走 GetWindowRect + SetWindowRect, 不受影响

**Caller migration**:

1. **保持 x/y 在 screen px 域里流转** — 大多数应用属于这条, 不用改:
   ```c
   int x, y, w_dip, h_dip;
   ui_window_get_rect_screen(owner, &x, &y, &w_dip, &h_dip);
   override.x = x;                 // screen px → screen px
   override.y = y;
   ui_page_open_window(p, &override);
   ```
   build 76-93 下错位, build 94+ 自洽对齐.

2. **持久化跨进程 + 跨 DPI 不漂移** — 之前靠 getter 内部除 DPI 自动
   "稳化", 现在需要 caller 显式转 DIP:
   ```c
   // save
   int dpi = ui_window_dpi(win);
   int x_dip = MulDiv(x_screen, 96, dpi);
   write_config("window.x", x_dip);

   // restore
   int x_dip   = read_config("window.x");
   int dpi     = GetDpiForSystem();   // 或建窗后再 ui_window_set_position
   int x_screen= MulDiv(x_dip, dpi, 96);
   cfg.x = x_screen;
   ```

   新增 helper API `ui_window_dpi(UiWindow) → int` (96/120/144/...).

3. **存了 DIP 的老 config 升级到 build 94+** — 老应用在 100% DPI
   主屏上写出 DIP=screen px, 升级后无感; 在 150% DPI 写出 DIP 值
   到 build 94+ 会被解释成 screen px, 窗口首次启动出现在偏左偏上
   ~33% 的位置 (一次性, 拖一下保存就回正常). 应用方根据需要做
   migration 或者吞了这次漂移.

GuoheView (本 lib 主消费者) 改动: image_info 子窗按主窗位置对齐的
workaround 直接生效 (它之前同时把 DIP 当 DIP 用, round-trip 没有 DPI
环节, 是少数 build 76-93 也正确的路径); current_geometry 持久化路径
存的是 DIP, 升级后第一次启动会漂一次, CHANGELOG 标记.

## 1.5.0 — build 93

### `white-space: nowrap` 接通 LabelWidget (L22)

CSS `white-space` 早在 cascade.cpp:103 白名单, renderer.cpp 也已支持
wrap=false 时自动 ellipsis trimming (DWrite SetTrimming), 但
widget_factory ApplyCss 没把 `white-space: nowrap` 桥接到 LabelWidget
SetWrap(false). 跟 build 92 的 line-height 同款 "实现到位但 CSS 没桥"
问题, 5 行 dispatch 修.

CSS:
  .x { white-space: nowrap; }   /* 单行 + 超出自动 "abc..." */
  .x { white-space: normal; }   /* 默认 wrap (跟缺省一致) */
  .x { white-space: pre-wrap; } /* 同 normal — pre 模式后续再加 */

用例: GuoheView image_info 窗 "镜头型号 / 镜头规格" 等长字段超出
容器宽度时自动 ellipsis 截断, 不换行撑高单行高度.

## 1.5.0 — build 92

### LabelWidget 接 CSS `line-height` 默认 1.3 × font-size (L20)

`LabelWidget::SizeHint()` 之前用 `fixedH > 0 ? fixedH : fontSize_ + 10.0f`
计算单行高度. 这个 +10 px 硬编码是 lib 早期给 badge / pill / chip 留
breathing 的副作用, 但应用到所有 label — 密集列表 / 表格类 UI 行高
偏松 (13 px 字 → 23 px 行, 23/13 = 1.77, 远超现代浏览器默认 1.3).

CSS `line-height` 属性早就在 cascade 白名单 (cascade.cpp:99), 但
widget_factory 没 apply 到 LabelWidget. 这版接通:
  - LabelWidget 加 SetLineHeightRatio(float) / SetLineHeightPx(float) +
    成员 lineHeightRatio_ / lineHeightPx_
  - widget_factory 在 ApplyCss 里解析 line-height — "1.3" → ratio /
    "17px" → px / 缺省 → ratio=1.3 默认
  - SizeHint h 公式改:
      lineH = lineHeightPx_>0 ? lineHeightPx_
            : lineHeightRatio_>0 ? lineHeightRatio_ * fontSize_
            : fontSize_ * 1.3f
      h = fixedH > 0 ? fixedH : lineH

行高对照 (默认 1.3, 跟现代浏览器 + Win11 Fluent UI 一致):
  - 13 px font: 旧 23 → 新 17 (-26%)
  - 14 px font: 旧 24 → 新 18 (-25%)
  - 16 px font: 旧 26 → 新 21 (-19%)
  - 20 px font: 旧 30 → 新 26 (-13%)

视觉影响: 自由排版的 label 行高变紧, 跟现代 UI 设计系统对齐.
有 fixedH (大部分 Button / Input / TitleBar) 的不受影响.
现有 .uix 想恢复旧行为可加 `label { line-height: 1.77 }`.

### `<window width>` DIP 单位文档化 (L21)

不是代码 bug, 但易踩坑: lib 的 `UiWindowConfig.width / height / x / y`
以及 .uix `<window width="...">` 都按 DIP (1/96 inch, CSS reference
pixel) 处理, 经 `MulDiv(dip, sys_dpi, 96)` 转物理像素. HiDPI 屏
用户填 "425" 实际渲染 width × dpi/96 物理像素 (1.5x 屏 → 638 px),
此前注释没说清.

ui_core.h UiWindowConfig 结构体顶部加 ~20 行 "单位语义" 注释 +
缩放表 (100%/125%/150%/200%); page_api.cpp `<window>` attr 解析
处加 inline 指向. 零代码改动 / 行为不变.

## 1.5.0 — build 91

### Toggle ON 态 thumb 暗色模式去黑改白 (L19)

`controls.cpp::UpdateCachedColors()` 之前写 `CachedThumbColorOn_ = dark ?
Rgb(0x00,0x00,0x00) : Rgb(0xFF,0xFF,0xFF)` — 暗模式 thumb 纯黑. 黑 thumb
在 Fluent 暗色 accent (≈ #63A2FF 提亮蓝) 上观感 OK, 但 lib 默认 accent
是品牌饱和中蓝 (≈ #115EA3), 黑 thumb 套上去视觉过重, 用户反馈 "丑".

修: ON 态 thumb 两个模式都返白 (`Rgb(0xFF,0xFF,0xFF)`). 跟 Material 3 /
iOS HIG / Tailwind / shadcn 主流路线对齐 — ON thumb 不区分 dark/light,
靠 track accent 色提供视觉 cue.

OFF 态不动 (light=深灰, dark=浅灰), 跟 track 形成柔和对比.

不动 accent token (`--accent`) — 这是品牌色, 改它影响所有 widget 主色调,
blast radius 远超 toggle 一个 widget.

## 1.5.0 — build 90

### SVG presentation attrs 父→子继承 (L18)

之前 `compiler.cpp` 处理 `<svg>` 子节点时不读父 `<svg>` / `<g>` 的 presentation
attrs (fill / stroke / stroke-width 等), 每个 shape 只看自己 attrs + CSS.
结果: 写 `<svg fill="none"><path stroke="..."/></svg>` (Heroicons / Lucide /
Tabler 等所有 outline icon set 的标准用法) 时, path 没自己的 fill attr,
SvgShape 默认 `hasFill=true` + 黑色 → outline icon 变成黑方块底 + 描边的
"一坨黑". GuoheView 右键菜单 4 个图标 (复制文件 / 另存为 / 打印 / 打开图片
位置) 直接撞这条.

修: walk lambda 多 `inherited` 参数, 进入 `<g>` 时合并它的 inheritable
attrs 后向下传; 处理 shape 时 **先** apply inherited (最低优先级) **再**
apply shape 自身 static attrs (覆盖 inherited) **再** apply CSS — 跟 SVG
1.1 §6.4 的继承规则 + CSS > presentation 的级联顺序一致.

继承白名单 (规范规定 inherited 的属性):
`fill / stroke / fill-opacity / stroke-opacity / stroke-width /
stroke-dasharray / stroke-linecap / stroke-linejoin / color`.
`opacity` 按规范不继承, 不在列.

`recomputeShapes` runtime 闭包同步: 每个 shape 编译期存一份 inherited
快照 (`shapeInheritedAttrs`), 闭包按值捕获. theme switch / hover 触发
recompute 时 apply 顺序跟编译期完全一致, 继承不会丢.

**没有 BREAKING**: 原来在每条 path 上重复写 `fill="..."` 的调用方继续生效
(child attr 仍然覆盖 parent); 原来漏 fill 的现在按规范继承父值.

## 1.5.0 — build 89

### menu autoclose debug flag 自动重置 (L17)

`ui_debug_set_menu_autoclose(0)` 之前是粘性全局 flag, 设了一次到 process 退出
都生效. 调试脚本/IPC 测过一次后, 后续业务用户右键所有菜单都不自动关 —
调试痕迹污染业务. 改 `ContextMenu::Close()` 时自动恢复 flag 到默认 (false =
autoclose ON). 调用方想持续抑制每次菜单显示前重新设置.

## 1.5.0 — build 88

### menu submenu 左移 — 补第二处 HandleMouseMove 路径 (L17)

build 86 加 `kSubmenuOverlap=6` 时漏改 HandleMouseMove 这条 (真鼠标 hover
打开 submenu 走的路径), 只改了 build 87 加的 OpenSubmenuAt (IPC 用). 用户
实际使用菜单还是看到 submenu 贴着 parent 不叠加. 两条路径都加 overlapPx,
鼠标 hover 时也走同款偏移.

## 1.5.0 — build 87

### menu autoclose 命中用"可见矩形"而不是 hwnd 整框 (L17)

build 86 加了 WM_ACTIVATEAPP 处理跨应用切换. 但用户报告同 app 内 click 外面
菜单不关. 排查发现 WM_TIMER Check 1 用的是 popup hwnd 整框 (GetWindowRect),
hwnd 比可见菜单大一圈 kShadowMargin=18 DIP 的阴影 padding. 用户 click 落
在阴影带 (视觉看着在菜单外, 但在 hwnd 内) 时, Check 1 判定 "click 在菜单
内" 漏掉关闭.

修: Check 1 / submenu / ancestor 三处命中判定都改用 "可见菜单矩形" —
hwnd rect 缩进 kShadowMargin × dpiScale 一圈, 跟用户看到的菜单卡片边界一致.

## 1.5.0 — build 86

### menu submenu 微微左移 + 箭头改 outline + 切应用即时关闭 (L17)

3 处一起改:

**1. submenu 左移 `kSubmenuOverlap = 6 DIP`**: 之前 submenu 紧贴 parent menu 右边
(`subX = rc.right - shadowMargin`), 视觉感受像两块独立板. 改成
`subX = rc.right - shadowMargin - kSubmenuOverlap`, submenu 微微叠在 parent
右边上, 跟 macOS / Win11 一致.

**2. submenu 入口箭头 ▸ → ›**: 之前 `▸ (▸ BLACK RIGHT-POINTING SMALL TRIANGLE)`
实心三角偏重. 改成 `› (› SINGLE RIGHT-POINTING ANGLE QUOTATION MARK)`
outline chevron, 跟 macOS / Win11 系统菜单同款克制风格. 字号从 kFontSizeSmall
改成 kFontSizeBody (13 → 14), 跟主文本同级看着更平衡.

**3. WM_ACTIVATEAPP 即时关菜单**: 之前菜单只靠 50ms 轮询 (WM_TIMER) 检查前台
窗口变化 来关闭. 用户切到其它软件后, 菜单要等下一个 tick 才关, 视觉有
0-50ms 延迟. 加 `WM_ACTIVATEAPP` (wParam=FALSE 表示 app 进入非激活态)
处理: 立即 KillTimer + Close. 跟原有 timer Check 2 互补.

注意: 同一应用内点击 main window 外 (但仍在 GuoheView 内, e.g. canvas 区域)
仍走 50ms timer Check 1 (LBUTTON + 光标在 menu 矩形外); 跨应用切换走 ACTIVATEAPP
立即关.

## 1.5.0 — build 85

### menu submenu 跟主菜单同宽 + 新增 submenu 测试 IPC (L17)

用户反馈 submenu 宽度不跟主菜单一致, 只撑到自己 items 的文本宽. 因为
submenu 是独立 ContextMenu, 自己算 MenuWidth, 只看到自己的 items.

**修复 1 — 宽度传播**:
- 新增 `ContextMenu::SetMinPropagatedWidth(w)` 接口
- `MenuWidth()` 把 `minPropagatedWidth_` 当下界 (跟 kMinWidth 一起)
- `PageState::PopulateMenu` 末尾算完 parent 的最终 MenuWidth, 走 compiledToMenu_
  把这个值回写到所有 submenu, 整族菜单同宽

**修复 2 — submenu 测试 IPC**:
之前 submenu 没法通过 IPC 触发 (menu_click_path 设计是 click leaf 不是 open),
debug / 截图 submenu 视觉很麻烦. 加:
- `ContextMenu::OpenSubmenuAt(int idx)` — 模拟 hover-open-submenu (跟实际
  mouse hover 同款 ShowPopup 逻辑), 返 sub ContextMenu*. 可链式 `parent->
  OpenSubmenuAt(i)->OpenSubmenuAt(j)` 走深层.
- C API `ui_debug_menu_open_submenu_path(win, path, depth)` — 沿 path 逐级
  open submenu, 不 click leaf.
- C API `ui_debug_screenshot_submenu_path(win, path, depth, outPath)` —
  combo: open submenu + 截图最深层的 popup.
- IPC `menu_open_submenu i0/i1/...` + `screenshot_submenu i0/i1/... <out>`.

**新增 public 方法**:
- `ContextMenu::MenuWidth() / MenuHeight()` — 之前 private, build 85 暴露给
  PageState 算 final 宽度后 propagate 用. 也方便 caller 查询 menu 实际宽.

**MenuWidth 公式扩展**:
```
w = maxContent + shortcutCol + arrowCol + 2*kPadding
w = max(w, minPropagatedWidth_)  // build 85+: tree-wide 共享
w = max(w, kMinWidth)            // build 84: sanity floor
```

## 1.5.0 — build 84

### menu kMinWidth 250 → 120 — 让 caller CSS width 真正生效 (L17)

build 83 改对了 max-width / width 语义, 但 `kMinWidth=250` 仍然挡住 caller
的精确控制 —— 用户写 `.menuitem-row { width: 100px }` 想让 menu 收到 ~228 DIP,
但 floor 250 把它撑到 250. floor 应该是"什么都没指定"时的兜底, 不该 override
caller 显式 CSS 控制.

降到 120 DIP — 短得不像菜单的下限. caller 想要 ≥ 120 都自己负责; 任何
显式 width / max-width / 自然撑出来的宽都正常生效.

## 1.5.0 — build 83

### menu max-width / width 改回正确 CSS spec — 撤回 build 82 折中 (L17)

build 82 把 `.menuitem-row` 的 `max-width` 当成 "想要的宽度" 用 (`w = maxW > 0 ?
maxW : SizeHint`), 跟 CSS spec 不符. spec 里 `max-width` 是**严格上界**, 不是
"撑到这个宽". 想要 "撑到 NNN", 应该用 `width: NNN`. 用户指出后撤回.

**正确语义** (跟浏览器 inline-block / block-with-explicit-width 一致):

```cpp
float w = SizeHint().width;          // 默认 fit-content; width:NNN 时 SizeHint 直接返 NNN
if (maxW > 0 && w > maxW) w = maxW;  // 上界 cap
if (minW > 0 && w < minW) w = minW;  // 下界提升
```

**调用方控制 menu content col 宽度**:
- `.menuitem-row { width: NNN }` → content col 永远 NNN, 想要"撑满"用这个
- `.menuitem-row { max-width: NNN }` → 上界 cap; content < NNN 时菜单较窄
- 两者都设, `width` 赢 (CSS 一致)

**反例 (build 82 错误做法, 已撤)**: 用 `max-width: NNN` 当 "撑到 NNN" 用,
是折中 patch — 跟 CSS spec 不一致, 不通用, 上层 caller 习惯浏览器 CSS 的会
踩坑.

## 1.5.0 — build 82

### menu max-width 用浏览器 block 语义 — 撑到 max-width 而非 fit-content (L17)

build 81 用 `SizeHint()` 当 reservedContentWidth, 是 fit-content / inline 行为
— content 自然宽小于 max-width 时不撑满, 跟浏览器 block element 不一致.
用户反馈: 浏览器里 `<div style="max-width: 240px">` 在父容器够宽时本应撑到
240px (block 行为), 不是只长出自然宽.

**修复**: measurement pass 改成 — 如果 wrapper 设了 `max-width` (CSS), 直接
用 max-width 当 content 宽 (block "撑到 max-width"); 没设时 fallback 走
`SizeHint()` 自然宽 (无明确 width 指示时的最后兜底).

```cpp
float w2 = (sub.root->maxW > 0)
    ? sub.root->maxW            // block: 撑到 max-width
    : sub.root->SizeHint().width; // 没 max-width → fit-content
```

**影响 user-facing**: `.menuitem-row { max-width: 100px }` 等于"我想让 content
列 100 DIP 宽", lib 老老实实用 100, 不再因为自然宽 < 100 就保留更小. 用户
可通过 max-width 直接控制 menu 总宽:

```
menu_width = max-width + shortcut col + arrow col + padding
           = max-width + ~128 DIP   (有 Ctrl+Shift+C 等长 shortcut)
           = max-width + ~32 DIP    (无 shortcut + 无 submenu)
```

要 menu 总宽 X DIP, 设 max-width = X - 128 (有 shortcut) 或 X - 32 (短菜单).

## 1.5.0 — build 81

### menu 反应式两态宽度**像素级**一致 (L17 微调)

build 80 还存在 ~20 px 两态宽度差, 因为我用 AST estimation (`<svg width=N> +
label text MeasureTextWidth + gap + padding`) 跟运行时 HBox SizeHint 不严格相等
(漏 margin / fontSize cascade / 各种 CSS 细节).

**修复**: 改成真实 build wrapper widget tree (通过现有 `CompileIterationTemplate`
跑完整 CSS 级联) 拿运行时同款 SizeHint, 跟 PopulateMenuItem 真实 add 进 menu
时用同一逻辑. 测得 maxContent 跟运行时严格相等, 加上 reservedShortcutWidth
+ reservedHasSubmenu 三个 floor 后, 两态 MenuWidth 完全相同.

**Trade-off**: PopulateMenu 每次 Show 多一遍 wrapper build pass (跟实际 populate
重复一次), cost ≈ N 个 items × CompileIterationTemplate 调用. 实测 10-20
items 每次 < 2ms, 用户无感.

**新增字段**:
- `ContextMenu::reservedContentWidth_` + `SetReservedContentWidth(w)`
- `PageState::PopulateMenu` 新增 measurement pre-pass

**验证 (GuoheView 右键菜单)**:
- 无图: 453 × 273 px
- 有图: 453 × 619 px
- **宽度完全相等**, 只高度因 items 数量不同而变.

## 1.5.0 — build 80

### menu 反应式两态宽度严格一致 + 整体收窄 (L17 微调)

build 79 ship 后用户反馈 menu 仍偏宽 (~510 px on-screen), 而且有图 / 无图
两态宽度仍有 ~60 px 差. 三处一起改:

1. **`kMinWidth` 300 → 250** + **`.menuitem-row` 默认 gap/padding/max-width
   全收紧** (gap 10→8, padding 12→8, max-width 180→140): menu 整体收窄
   ~60 px, on-screen ~440 px (Win 1.65x DPI).
2. **新增 `ContextMenu::SetReservedHasSubmenu`**: 跟 build 78 加的
   `SetReservedShortcutWidth` 配对 — PageState 扫所有 CompiledMenuItems
   (含 v-if=false 的) 看有没有 submenu, 喂给 menu. MenuWidth 始终保留
   arrow col, v-if 切换 visible items 集合不影响 arrow 列宽.
3. **PageState 扫 menu 时同时扫 max shortcut + has submenu**: 两个 reserved
   值都从全 items 算 (含 v-if=false 的). 配合 kMinWidth=250 把两态自然
   宽度差 (主要来自 content 自然宽: 打开图片位置 126 DIP vs 无边框模式
   112 DIP, 差 14 DIP) 吸收掉, 实际两态视觉宽度差 <10 px.

**影响**:
- `src/ui/context_menu.h` — kMinWidth + reservedHasSubmenu_ 字段 + setter
- `src/ui/context_menu.cpp` — MenuWidth 用 reservedHasSubmenu 当 floor
- `src/ui/page/page_state.cpp` — scan all items 同时算 max shortcut + any submenu
- `src/ui/page/page_api.cpp` — 默认 `.menuitem-row` CSS 收紧

## 1.5.0 — build 79

### menu kMinWidth 280 → 300 DIP (L17 微调)

build 78 ship 后用户反馈 280 DIP 仍偏窄, 调到 300 DIP 跟 Win11 / macOS 常见
右键菜单宽度对齐. content 自然宽 < 300 时仍 floor 到 300; 长内容自然撑过
300 也照常。

## 1.5.0 — build 78

### menu 收紧 MenuWidth + 反应式两态宽度一致 (L17 跟进)

build 77 加了 `.menuitem-row { max-width: 240px }` 默认, 但实际 GuoheView 右键菜单
"有图" / "无图" 两态宽度仍不一致 — 主因是 v-if 隐藏含 shortcut 的 items 时,
maxShortcut 缩水, menu width 跟着窄了 ~80 DIP. 用户视觉上同一个菜单变窄变宽
两次, 体感不稳.

修复 3 处一起:
1. **`MenuWidth` 收紧 fudge**: shortcut col padding 24 → 16, 末尾 +16 fudge 整体
   删. 同 content 状态下 menu 宽 -24 DIP, 视觉更紧凑.
2. **`kMinWidth` 200 → 280**: 短菜单 floor 提高, 跟典型有 shortcut 菜单的宽度
   接近, 短状态下不至于缩到 200 看着 "缺东西".
3. **新增 `ContextMenu::SetReservedShortcutWidth`**: PageState 在 PopulateMenu
   时扫所有 CompiledMenuItems 的 shortcut (含 v-if=false 的) 算 max, 喂进
   menu, MenuWidth 把它当 shortcut col floor. v-if 不影响 shortcut 列宽度,
   两态视觉一致 (visible items 无 shortcut 时, 那一列仍预留空间).
4. **默认 `max-width` 240 → 180**: 配合上面更紧凑的 menu 自然宽, 短中文 +
   常见 shortcut 组合下不需要超过 180 DIP 内容宽.

**影响文件**:
- `src/ui/context_menu.h` + `.cpp` — `MenuWidth` 公式收紧 + `reservedShortcutWidth_`
  字段 + setter; `kMinWidth` bump
- `src/ui/page/page_state.cpp` — `PopulateMenu` 扫 all-items shortcut, set 到 menu
- `src/ui/page/page_api.cpp` — 默认 `.menuitem-row` max-width 240 → 180

## 1.5.0 — build 77

### menu 内置默认 CSS — 可被用户 .uix 覆盖 (L17 跟进)

build 75-76 ship 后, 集成测试发现菜单加载图后宽度 ~640px 偏宽 — 主因是
没有 `max-width` 限制, 最长 menuitem ("打开图片位置" + "Ctrl+Shift+C")
自然宽叠加 padding 后撑到 ~290 DIP, 高 DPI 屏放大到 ~580 px 视觉过宽.

修复思路: 跟主流 Web/Vue 习惯一致 —— **lib 提供合理默认 CSS, 用户可在
自己 .uix `<style>` 写同名选择器覆盖**. 默认 `.menuitem-row` 加
`max-width: 240px` 等收紧约束; 用户场景下若想放宽 (e.g. 长 i18n 文本)
写 `.menuitem-row { max-width: 400px }` 即可覆盖.

**默认规则** (在用户 CSS 之前 parse 进 stylesheet, 同选择器 user 后写者赢):
```css
.menuitem-row {
  flex-direction: row;
  align-items: center;
  gap: 10px;
  padding: 0 12px;
  max-width: 240px;
}
.menuitem-row svg { width: 18px; height: 18px; flex: none; }
.menuitem-row label { font-size: 13px; }
```

**为什么不用 inline style**: build 75-76 之前把这些放在 wrapper 的 inline
`style="..."` 上, 用户后续没法用 class CSS 覆盖 (inline 永远赢 specificity).
build 77 把 inline 全删, 改走 class selector, 让 cascade 自然工作.

**改动文件**:
- `src/ui/page/page_api.cpp` — compile 前 inject lib 默认 menu CSS
- `src/ui/page/compiler.cpp` — wrapper 不再写 inline style, 只保留 class

## 1.5.0 — build 76

### menuitem widget-slot 收尾修复 (L17 跟进)

build 75 集成验证发现的 3 个真实问题, 都在 GuoheView 把右键菜单切到声明式
template 后撞上, 一起修:

1. **SVG `<g>` 子节点无法渲染**: compiler.cpp 的 SVG walk lambda 只识别
   `<defs>` / `<linearGradient>` / `<radialGradient>`, 撞到 `<g>` 直接
   `k < 0` skip 整棵子树. 实际 SVG 里 `<g fill="none" stroke="currentColor">`
   包 path 是很常见的写法, 用户的设置图标 / 退出图标都是这种结构, 装到
   widget tree 后 path 完全不显示. 修: 跟 `<defs>` 同款 walk 递归处理 `<g>`
   children (presentation attrs 暂未级联, 调用方继续在 path 上写 fill 等).

2. **menuitem wrapper 默认 column 布局**: compiler.cpp 给 `<menuitem>` 合成
   的 `<div class="menuitem-row">` 没默认 inline style, widget_factory 解析
   div 时 lookup `flex-direction` CSS, 没找到默认 VBox. 用户必须自己写
   `.menuitem-row { flex-direction: row }` CSS, 不友好. 修: wrapper 加 inline
   `style="flex-direction: row; align-items: center; width: 100%; height: 100%"`
   兜底, 用户 CSS 仍可覆盖 (cascade level).

3. **PopulateMenuItem WatchEffect 持 dangling Widget***: PopulateMenu 在每次
   Show 时重 build widget tree (跟 v-for 同款), 老 widget 析构. 但 WatchEffect
   注册时捕获 widget 指针, 没 dispose, deps 变化时回调里访问 dangling 指针
   → crash + 应用色没更新. 修: menuitem binding 改成"PopulateMenu 时一次性
   eval", 不挂 long-lived effect. 反正每次 Show 都重 build, eval 一遍足够,
   menu 关闭后 widget tree 也马上销毁, 不需要反应跟随.

**影响文件**:
- `src/ui/page/compiler.cpp` — SVG walk 加 `<g>` 透传; menuitem wrapper 加
  inline flex-row style
- `src/ui/page/page_state.cpp` — PopulateMenuItem 改一次性 eval

**回归**: 老的 markup_builder MenuBar 路径不受影响 (仍走 LabelWidget 直接构造).
Demo / 现有 .uix 老用法不动一行.

## 1.5.0 — build 75

### BREAKING — menuitem 重设计成 widget 模板 (L17 跟进)

**动机**: 老的 `<menuitem text="..." icon="...">` 静态字段 + build 73 加上的
`:text` / `:icon` / `:style` 反应式 attrs 都是"专属 API", 给了每个想法都得加新
attr 才能用 (e.g. 想只让 icon 变色不让 text 变, 只能加新 `:icon-style` attr).
跟 widget 主路径 `<div :style="...">` 不一致, 学习成本翻倍.

**修复**: menuitem 改成 widget 容器 slot. body 内任意 widget 自由嵌套, 反应式
attrs (`:style` / `v-if` / `:class` / `{{ }}` / `v-for` / `@click` ...) 都跟普通
widget 同款生效. ContextMenu 端走 Widget::DrawTree + Widget::DoLayout 渲染.

**新写法**:
```uix
<menu trigger="#x" event="rclick">
  <menuitem id="1" shortcut="Ctrl+C">
    <svg :style="`color: ${pinned ? '#0078D4' : ''}`">...</svg>
    <label>复制图片</label>
  </menuitem>
  <menuitem id="2"><label>{{ $t('paste') }}</label></menuitem>
  <separator v-if="hasMore"/>
  <menuitem v-for="x in items" :id="x.id"><label>{{ x.name }}</label></menuitem>
</menu>
```

**保留 menuitem-level attrs** (跟 menu 语义强相关):
- `id` (静态 int) — 派发 callback
- `shortcut` (静态) / `:shortcut` (反应式) — 右对齐显示快捷键文字
- `onclick="method"` / `@click="method"` — JS handler name
- `:enabled="bool"` — 反应式 disable (整 item dim + 不响应 hover/click)
- `v-if` / `v-show` / `v-for` — 同 widget 标准

**body slot 规则**:
- Element 子节点 (svg / label / div / button / 任意 widget): 原样作 child widget
- Text 子节点 (非全空白): 自动包成 `<label>` 简化常见写法
- Interpolation `{{ }}` 节点: 自动包成 `<label>{{ x }}</label>`
- body 所有内容塞进合成 `<div class="menuitem-row">` 容器 (flex hbox 语义, 见 default CSS)

**BREAKING — 删除的 API / attrs**:
- C API: `ui_menu_add_item` / `ui_menu_add_item_ex` / `ui_menu_add_submenu(text, sub)` — imperative 构造路径完全去掉, 菜单只走 .uix
- C++ class: `ContextMenu::AddItem` / `AddItemEx` / `AddItemBitmap` / `SetLastItemColor` — 用 `AddItemContent(id, shortcut, widget)` / `AddSubmenu(widget, sub)` 代替
- menuitem static attrs: `text` / `icon` / `style="color:..."` — body widget 自己解决
- menuitem bound attrs: `:text` / `:icon` / `:style` — body widget 自己 reactive
- MenuItem struct fields: `text` / `iconSvg` / `imgSrc` / `hasIcon` / `bitmap` / `hasColor` / `overrideColor` — 全砍, 留 `customContent: WidgetPtr` 一个

**仍工作 / 改动小**:
- C API: `ui_menu_create` / `destroy` / `add_separator` / `set_enabled` / `set_bg_color` / `set_corner_radius` / `show` / `close` — lifecycle + 全局视觉
- 嵌套 `<menu title="..." v-if="...">` 当 submenu — title 自动包 `<label>` 当 entry widget; submenu 内部 items 跟主 menu 同款规则

**迁移示例**:
```diff
- <menuitem id="1" shortcut="Ctrl+C">
-   <svg viewBox="...">...</svg>
-   复制
- </menuitem>
+ <menuitem id="1" shortcut="Ctrl+C">
+   <svg viewBox="...">...</svg>
+   复制                <!-- 自动包 label, 写法不变 -->
+ </menuitem>
```
**纯静态写法 (`<menuitem><svg/>文字</menuitem>`) 不用动**, compiler 自动 wrap
loose text 到 label, demo 全部无修改. 真正需要迁移的是用了 `:text` / `:icon` /
`:style` 反应式的菜单 — 改写成 body 里 `<label :class>` `<svg :style>` 等.

**性能 / 风险**:
- 每次 ContextMenu Show 重 build customContent widget tree (CompileIterationTemplate
  + bindings wire), 跟 v-for 同款 cost — 10 items 估算 < 2ms, 用户无感
- popup hwnd 内 widget Layout / Draw 跟主窗口走同一套渲染管线, 主题 / CSS 级联
  自动生效

**改动文件**:
- `src/ui/page/compiled_page.h` — CompiledMenuItem 砍老字段 + contentRoot AST
- `src/ui/page/compiler.cpp` — menuitem children → 合成 `<div class="menuitem-row">` wrapper, text auto-wrap label
- `src/ui/context_menu.h` + `.cpp` — MenuItem 砍字段加 customContent; Draw 走 DrawTree; AddItemContent / AddSubmenu(widget,sub) 新 API
- `src/ui/page/page_state.cpp` — PopulateMenuItem 走 CompileIterationTemplate + WatchEffect 同 v-for 路径
- `include/ui_core.h` + `src/ui/ui_api.cpp` — 砍掉老 imperative C API
- `CHANGELOG.md` / `CMakeLists.txt` / `VERSION` / `version.json` — bump 74→75

---

## 1.5.0 — build 74

### WireMenus 顶层菜单接通反应式路径 (L17 跟进)

build 73 把反应式 + icon 完整支持都集中在 `PageState::WireSubtreeMenus`,
但 `WireMenus` (顶层菜单入口, AttachQuickJS 第 9 步调) 仍走老的静态简化路径,
build items 用 `AddItemEx(.., "", nullptr)` —— 既不挂 beforeShowHook (反应式
失效), 也不传 svg/imgSrc (图标空白). GuoheView 改成声明式 `<menu trigger="#canvas_body">`
后看到的就是: 顶层菜单 v-if/v-for 不响应, :icon 不渲染, :style 不变色.

修复: `WireMenus` 直接 delegate 到 `WireSubtreeMenus(page_.menus, menus_)`,
两路统一. 老 demo 用静态 `<menuitem text="..." icon="...">` 同样工作 (PopulateMenuItem
对 bound 字段为空时走静态 fallback, 向后兼容).

## 1.5.0 — build 73

### Menu 全反应式 — 对齐 Vue 3 (L17)

**问题**：build 26 起 `<menuitem>` 仅支持静态 attrs，无法用 `v-if` / `v-for` /
`:style` / `:icon` / `{{ }}` 等反应式语法。`ContextMenu::SetLastItemColor`
等内部能力存在但没暴露给 .uix 声明式路径，docs/controls.md 里写的
`ui_menu_set_last_item_color` 也只是文档，没真实 C API。

**修复**：把菜单接进通用反应式 pipeline。`<menu>` / `<menuitem>` /
`<separator>` 的 bound attrs 编译期收集成 expr 字符串；运行期 ContextMenu
Show 入口走新的 `PageState::PopulateMenu` 路径，按当前 JS state 重 eval
+ rebuild items。同套 expr_rewriter + QuickJS closure 跟 widget binding
共享, 没引入新 runtime 概念。

**新增反应式 attrs (全 Vue 3 对齐)**：

`<menu>`:
- `v-if` / `v-show` — 整菜单条件渲染 (popup 模型下两者等价)
- `:title` — 嵌套 submenu 标题反应式 (静态 `title` / `text` attr 保留)

`<menuitem>`:
- `v-if` / `v-show` — 单项条件渲染
- `v-for="x in items"` / `"(x, i) in items"` — 列表展开, scoped 变量在
  同一 menuitem 的其它 bound attrs 里可见
- `:text` / `{{ }}` 文本插值 — body 反应式 (跟 widget 同套 BuildTextSourceJs)
- `:icon` — auto-detect: 以 `<svg` 开头当 inline SVG; 否则当 asset key
- `:style` — 提 `color: X` 子串映射 SetLastItemColor (跟静态 style 同语义)
- `:shortcut` — 快捷键显示文本
- `:enabled` — false → disabled

`<separator>`:
- `v-if` / `v-show` — 条件分隔符

`<widget>` (顺道补全):
- `v-show` 早已存在 (compiler.cpp 老逻辑 desugar 到 `:visible`), 这次
  把它在 menu 上也接通, 整库 v-show 语义一致

**重求值时机**: 每次 `ContextMenu::Show` / `ShowPopup` 入口调
`beforeShowHook` → `PopulateMenu` 全量 rebuild items。理由: popup UI,
关闭后状态变化下次自动反映, 用户体验等同 Vue 立即响应; menu 不挂 dep
watcher 实现成本低 + 运行 footprint 小。每次重 build cost ~10 items ×
QuickJS closure compile+eval, 实测 < 1ms。

**新 API**:
- `ContextMenu::Clear()` — 清 items_, PopulateMenu 内部用
- `ContextMenu::SetBeforeShowHook(fn)` — PageState 在 WireSubtreeMenus
  时给每个 ContextMenu 注册. 老的 imperative `ui_menu_create` 路径不设,
  保持向后兼容
- `PageState::PopulateMenu/PopulateMenuItem` — 内部反应式 rebuild 入口
- `PageState::EvalBoundExpr/EvalBool/EvalString` — 私有 eval 辅助

**向后兼容**:
- 老 `ui_menu_create` / `ui_menu_add_item_ex` / `ui_menu_show` C API 完全
  不变, 不设 beforeShowHook 时 items 保持调用方建好的样子
- 老 `<menuitem style="color: red">` 纯静态写法继续工作 (compiler 走
  AttrKind::Static 分支照旧 parse)
- 老 `<menu trigger="#btn" event="click">` 含静态 menuitems 的 demo
  不动一行, build 通过, 视觉无变化

**示例**:
```uix
<script>
export default {
  data() { return { pinned: false, hasImage: false }; },
  computed: { pinColor() { return this.pinned ? '#0078D4' : ''; } }
}
</script>
<menu trigger="#canvas" event="rclick">
  <menuitem v-if="hasImage" id="11" shortcut="Ctrl+C" :icon="svgCopy">复制图片</menuitem>
  <separator v-if="hasImage"/>
  <menuitem id="15" :icon="svgPin" :style="`color: ${pinColor}`">窗口置顶</menuitem>
  <menu v-if="hasImage" title="更多">
    <menuitem id="17" :icon="svgWallpaper">设为桌面背景</menuitem>
  </menu>
</menu>
```

**改动文件**:
- `src/ui/page/compiled_page.h` — `CompiledMenuItem` / `CompiledMenu` 加
  bound expr 字段
- `src/ui/page/compiler.cpp` — menu / menuitem / separator attr 扫描加
  AttrKind::Bind + AttrKind::Directive 路径
- `src/ui/context_menu.h` + `.cpp` — `Clear()` + `beforeShowHook_` +
  Show/ShowPopup 入口 call hook
- `src/ui/page/page_state.h` + `.cpp` — `PopulateMenu`/`PopulateMenuItem`
  + `EvalBoundExpr` 系列; `WireSubtreeMenus` 重构成只建 shell + 注册 hook

---

## 1.5.0 — build 72

### gh-img-view RenderSvgToBgra DPI 修复 (L21 跟进)

build 71 的 `ui_gh_img_view_render_svg_to_bgra` 在 HiDPI 显示器 (e.g. 144 / 192
DPI) 上把 SVG 渲染到 offscreen bitmap 时, 内容只填了 bitmap 的左上角一部分,
其余透明. 原因: 主 context 的 DPI 跟显示器 scale 走 (1.5x → 144), offscreen
bitmap 创建时 DPI=96 但 context 切 target 后**不会**自动改 DPI, 我的
`Scale(out_w/svgW)` 解释成 DIPs 在 144 DPI 下放大 1.5x, SVG 内容溢出 bitmap 边界.

修复: `SetTarget(gpuBmp)` 后立即 `SetDpi(96, 96)`, render 完 `SetDpi(oldDpi)`
恢复. 保证 DIPs ↔ pixels 1:1, Scale 落到 bitmap 像素严格对齐.

## 1.5.0 — build 71

### GhImgView SVG 加缩略图光栅化 API (L21)

**新能力**: SVG 矢量源加载后, 可以把它光栅化到 caller 缓冲, 用于鸟瞰图 / 缩略图
等需要 BGRA 像素的场景. fit 保 aspect, 实际像素尺寸经 out_w/out_h 回填.

**新 API**:
```c
UI_API int ui_gh_img_view_render_svg_to_bgra(UiWidget w,
                                                uint32_t target_w,
                                                uint32_t target_h,
                                                uint8_t* out_bgra,
                                                uint32_t* out_w,
                                                uint32_t* out_h);
```
- 返 0 OK / 非 0 失败 (-1 = 未加载 SVG, -2 = 参数错, -3 = D2D 不支持, 其他
  内部 D2D 错误).
- out_bgra 由 caller 分配, 至少 target_w*target_h*4 字节. 实际写入 packed BGRA8
  premul, 大小 = out_w*out_h*4.
- 内部用 off-screen ID2D1Bitmap1 (TARGET) + ID2D1Bitmap1 (CPU_READ) +
  ID2D1DeviceContext5::DrawSvgDocument 走完整 D2D 路径, 矢量光栅化质量同主画布.

**为什么**: GuoheView 鸟瞰图 (minimap) 需要 BGRA 缩略图, 而 SVG 是矢量源没有像素
流, 之前 SetSvgFromFile 后 thumb_bgra_ 留空导致鸟瞰图自动隐藏. 调用方不该自己开
D2D 上下文做 off-screen render, 所以 lib 提供该 API (rule: lib UI bug 优先通用修
复, 把矢量→栅格能力暴露在 widget 上).

**实现要点**:
- 临时 SetTarget 到 off-screen bitmap, 渲染完恢复主 target, 不影响外层 paint cycle
- 调用方应在 paint cycle 外调 (SetSvgFromFile 加载完后立即调一次填缩略图)
- 主 ctx_ 和 off-screen bitmap 同一 device, CopyFromBitmap 走 GPU 直传不经 CPU

## 1.5.0 — build 70

### GhImgView 加 SVG 矢量源 — 复用瓦块路径的 viewport/zoom/pan/rotation (L20)

**新能力**: `GhImgViewWidget` 现在可以吃 SVG 文件作为渲染源, 跟瓦块 pyramid
并存. 加载 .svg 后 widget 进入 "SVG 模式" — OnDraw 走
`ID2D1DeviceContext5::DrawSvgDocument` 直接矢量光栅化, 不再画瓦块. viewport
(zoom/pan/rotation/Fit) 全部复用现有路径代码 — SVG natural size (优先
viewBox, fallback width/height) 写进 `info_.fullWidth/Height`, 几何完全一致.

**新 API**:
```c
UI_API int ui_gh_img_view_set_svg_file(UiWidget w, const wchar_t* path,
                                          uint32_t* out_w, uint32_t* out_h);
UI_API int ui_gh_img_view_is_svg_mode(UiWidget w);
```
- 返 0 OK / 非 0 失败 (文件不存在 / SVG 解析失败 / Win10 1607 前
  CreateSvgDocument 不可用). 失败时 widget 状态不变.
- out_w/out_h 出 SVG natural size, 可为 NULL.
- 跟 `set_pyramid_image` / Begin+SetTile 系列互斥 — 调一个清另一个.

**架构**: 加 `ComPtr<ID2D1SvgDocument> svgDoc_; uint32_t svgW_, svgH_;` 私有
字段. `OnDraw` 入口判 `svgDoc_` 非空就短路, transform 用跟瓦块同款的 Scale +
Rotate-around-dest-center 公式 (照搬 `image_source_svg.cpp:29-69`). `Begin`/
`Clear` 顺带清 svgDoc_, 避免 source type 串图.

**典型用法** (GuoheView 看图器):
```c
uint32_t w, h;
if (ui_gh_img_view_set_svg_file(canvas, L"icon.svg", &w, &h) == 0) {
    /* 跟普通图同款 — zoom/pan/rotate 都生效 */
}
```

**ABI**: 2 个新导出 + 内部字段, 旧 API 不变.

## 1.5.0 — build 69

### Menu 默认稍宽 (180→200) + 圆角可调 (L19)

**行为变化**:
1. 菜单 `kMinWidth` 默认 180 → 200, 多 20px 给文字 + 短横线 + 子菜单箭头
   呼吸感. 单字段宽 (item.text * 7.5 等) 算出的实际宽如果超过 200 仍然按
   实际算, kMinWidth 只是下限.
2. 新加 `ui_menu_set_corner_radius(menu, r)` API. 单菜单覆盖默认 10px 圆角.
   <0 = 用默认. 影响 shadow + card bg 两个 `FillRoundedRect`. hover item
   highlight (6px 圆角) 不动 — 它是 item 级视觉, 不属于容器圆角.
   子菜单单独设, 不继承父菜单 — 调用方可以给每个 menu 独立配.

**API**:
```c
UI_API void ui_menu_set_corner_radius(UiMenu menu, float radius);
```

**用法**:
```c
UiMenu m = ui_menu_create();
ui_menu_set_corner_radius(m, 6.0f);   // 紧凑型, 圆角小一点
ui_menu_add_item_ex(m, ...);
ui_menu_show(win, m, x, y);
```

**ABI**: 加 1 个新导出 + ContextMenu 类加 `cornerRadius_` 字段. 旧代码不需要
任何改动, 行为变化仅是默认宽度从 180 → 200.

## 1.5.0 — build 68

### Toast 动画改 time-based, 解 WM_TIMER 合并 tick 时掉帧 (L18)

**行为变化**: `ui_toast` 弹出 / 隐去的滑入滑出动画在系统忙时不再 stall.
phase / slide 的推进改成读 `GetTickCount64()` 起点 delta, 跟 `animation.cpp`
里 `Animation::Tick` 同模式 (time-based + clamp).

**旧 bug** (`ui_window.cpp:829-857`):
```cpp
case 1: toastSlide_ += 0.08f;  // 每 16ms tick 固定推 0.08
case 3: toastSlide_ -= 0.06f;
```
动画进度 = tick 数 × 固定增量, 不读实时. WM_TIMER 是低优先级消息, OS 忙
(其他 animation / 后台 paint / IPC pipe pump) 时会合并 tick → 真实推进速度
跟着 timer 频率漂, 视觉是"滑到一半卡住".

**修复**: phase / slide 从 `now - toastShownTick_` 派生:
```cpp
const uint64_t elapsed = GetTickCount64() - toastShownTick_;
const uint64_t t1 = kToastSlideInMs;            // 200
const uint64_t t2 = t1 + holdDurationMs_;
const uint64_t t3 = t2 + kToastSlideOutMs;      // 250

if      (elapsed < t1) { phase=1; slide=elapsed/t1; }
else if (elapsed < t2) { phase=2; slide=1; }
else if (elapsed < t3) { phase=3; slide=1 - (elapsed-t2)/(t3-t2); }
else                    { phase=0; cleanup }
```

丢 tick 时下次 tick `elapsed` 跳跃增加, slide catch-up 到正确值. 即便
framerate 降到 30/20fps, 视觉是 "帧间隔大但进度连续", 不再有 stall.

**不修的**: WM_TIMER 自身 ~15.6ms resolution + 合并问题 (Win32 timer 默认
低优先级). 想 60fps 需要 `timeBeginPeriod(1)` (系统级影响) + timer queue
或 DWM-sync invalidate, 那是更大架构改动. 这次只解 stall 问题, framerate
本身留以后.

**ABI**: 加 `UiWindowImpl::toastShownTick_` 字段, 公共 API 不变 (`ui_toast`
签名同).

## 1.5.0 — build 67

### Submenu 紧贴父菜单, 去除 18px 透明空白 (L17)

**行为变化**: 含 submenu 的 popup menu, hover 父项弹出 submenu 时, submenu
可见卡片紧贴父菜单可见卡片右边 (edge-to-edge), 不再有透明间隙.

**旧 bug** (`context_menu.cpp:705-712`):
```cpp
GetWindowRect(popupHwnd_, &rc);
int subX = rc.right;   // ← BUG
sub->ShowPopup(..., subX, subY);
```
popup hwnd 比 "可见菜单卡" 大一圈 (各边 kShadowMargin=18px, 留给 drop
shadow). `GetWindowRect` 返外圈 hwnd rect, 即 `rc.right` 比可见卡右边
多 18px. 把这个值当 anchor 给 submenu `ShowPopup`, 而 `ShowPopup` 内部
又把 hwnd 整体左移 18px 给 submenu 自己 shadow 留位置. 净效果: submenu
可见卡左边 = 父可见卡右边 + 18px → **18px 透明间隙**.

100% DPI 18 px, 125% DPI 22.5 px, 150% DPI 27 px. 视觉割裂.

**修复** — 一行: 减掉 marginPx 拿可见卡右边作 anchor.
```cpp
int marginPx = (int)(kShadowMargin * subScale);
int subX = rc.right - marginPx;
```
subY 不用改 (验证过当前 anchor 跟 item top 对齐).

**ABI**: 纯实现修, 兼容.

## 1.5.0 — build 66

### `ui_widget_on_mouse_wheel` 通用 widget 滚轮回调 (L16)

**行为变化**: 新增 `ui_widget_on_mouse_wheel(w, cb, ud)` API, 任意 widget 类型可挂滚轮回调. 之前 `ui_custom_on_mouse_wheel` 只挂在 `CustomWidget::mouseWheelCb` 上, 而 `UiWindowImpl::OnMouseWheel` (`ui_window.cpp:1865-1902`) bubble loop 写死只识别 5 个 widget 类型 (TextArea/ImageView/ImageViewPlus/GhImgView/ScrollView), CustomWidget 不在列表里 — `mouseWheelCb` 永远不被调用. 这次走 `Widget::onMouseWheelHook` 路径 (跟 `.uix @wheel` 同一条), 在 `OnMouseWheel` 开头无条件 fire, 不挑 widget 类型.

**旧 bug**: `<custom>` widget 设了 wheel 回调, 滚轮不触发. 同样的 widget 上 mouse_down / move / up 正常工作, 因为它们走 `OnMouseDown` 等多态 method dispatch, 不挑类型.

**API**:
```c
typedef void (*UiWidgetWheelCallback)(UiWidget w, float x, float y,
                                       float delta, void* userdata);
UI_API void ui_widget_on_mouse_wheel(UiWidget w,
                                       UiWidgetWheelCallback cb,
                                       void* userdata);
```
- x/y 是 widget 内坐标 (跟 `ui_widget_on_mouse_move` 同模式 — 减去 rect.left/top)
- delta 正值上滚 / 负值下滚, 跟 WM_MOUSEWHEEL WHEEL_DELTA 同号 (典型 ±120)
- cb=NULL 解绑

**用法**:
```c
UiWidget w = ui_widget_find_by_id(root, "my_canvas");
ui_widget_on_mouse_wheel(w, on_wheel, self);
```
跟 `ui_custom_on_mouse_wheel` 互斥但**不互冲** — 旧调用方仍工作 (lib dispatch loop 一旦扩了 CustomWidget 也会同时跑), 但建议新代码用通用 API.

**ABI**: 1 个新导出. 旧 `ui_custom_on_mouse_wheel` 兼容保留 (虽然对 custom widget 本身仍然没用 — 因为根因是 dispatch loop, 不是 API 注册; 想真正生效请用 `ui_widget_on_mouse_wheel`).

## 1.5.0 — build 65

### `UiWindowConfig::owner` 子窗口附属主窗口 (L14)

**行为变化**: `UiWindowConfig` 加 `UiWindow owner` 字段, 非 0 时新窗口
附属于该 owner 窗口 — Z-order 在 owner 之上, owner 最小化 / 关闭时跟随,
不在 Alt+Tab / taskbar 单独列一项. 适合"设置对话框 / 偏好窗 / 子工具窗
附属主窗口"的桌面应用标准模式.

**旧 bug**: lib `CreateWindowExW` 的 `hwndParent` 硬编 `nullptr` (
`ui_window.cpp:220`), 所有窗口都是孤儿顶级窗. 想做附属子窗的应用 (VSCode
风格设置 / Photoshop 偏好) 只能在 lib 外面 `SetWindowLongPtrW(child,
GWLP_HWNDPARENT, main)` 事后绑, 已经走完一遍 CreateWindowEx 不完全等价.

**修复**:
```c
typedef struct UiWindowConfig {
    ...
    UiWindow owner;   /* 0 = 顶级 (默认行为不变); 非 0 = 附属窗 */
} UiWindowConfig;
```

`ui_window_create` 内部用 `Ctx().GetWindow(config->owner)->Handle()` 拿
HWND, 透传给 `UiWindowImpl::Create(...)` 的新参数 `HWND ownerHwnd`. Create
里:
- `CreateWindowExW(...ownerHwnd...)` — 取代原本的 `nullptr` 第 9 参数
- 当 `ownerHwnd != nullptr` 时不加 `WS_EX_APPWINDOW` (这条 ex style 跟
  owner 语义冲突 — owner 窗本来就不上 Alt+Tab / taskbar). `tool_window=1`
  路径不动 (WS_EX_TOOLWINDOW 跟 owner 兼容, 想"既 owner 又工具"的组合
  可以两个都开).

**典型用法** (子窗附属于主窗):
```c
UiWindowConfig override = {0};
override.owner = main_window_handle;   /* 附属主窗 */
override.tool_window = 1;              /* 想再去掉 taskbar 项也可以叠加 */
UiWindow settings = ui_page_open_window(settings_page, &override);
```

**owner ≠ child**: Win32 顶级窗口的 `hwndParent` 参数实际是 "owner" 关系,
不是 child window 关系 (后者会限制在父窗矩形里, 行为完全不同). lib 这次
用的就是 owner 路径 — 子窗仍然是顶级窗, 自由移动, 只是生命周期绑 owner.

**ABI**: `UiWindowConfig` 结构体加字段. 旧代码 `= {0}` / `= {}` 零初始化
继续工作 (`owner = 0` = 顶级窗, 默认行为不变). 旧二进制重链才能用 — 不
保留二进制兼容.

## 1.5.0 — build 64

### `<custom>` 可进键盘焦点系统 + widget on_focus/on_blur C API (L13)

**行为变化**: `ui_custom_set_focused(w, 1)` 不再只是改一个 paint bool, 它会把
widget 推进 owner window 的 `focusedWidget_` 槽 — 让 WM_KEYDOWN 真正路由到
custom widget 的 `keyDown` 回调, 同时 widget 的 `onFocusHook` / `onBlurHook`
正确触发. 鼠标点其他 widget 时, lib 内部的 mouse_down 分发会自动调
`SetFocus(otherWidget)`, 旧 focused 的 widget 失焦触发 blur (前提: focusable=true).

**旧 bug**:
- `ui_custom_set_focused` 只动 `CustomWidget::focused` 一个 paint bool, **不**
  调 `UiWindowImpl::SetFocus`. 调用方拿不到键盘.
- `CustomWidget` ctor 没有 `focusable = true` (对比 ButtonWidget / Slider /
  TextInput / Toggle 等原生 widget 都在 ctor 显式置位). 鼠标点击 dispatcher
  在 `ui_window.cpp:1611-1619` 处:
  ```cpp
  Widget* target = root_->HitTest(dx, dy);
  while (target && !target->focusable) target = target->Parent();
  SetFocus(target);
  ```
  custom widget 不 focusable → target 跳到上级 → `focusedWidget_` 永远不是
  custom widget → WM_KEYDOWN 在 dispatcher 末段 `focusedWidget_->OnKeyDown(vk)`
  路由不到. 表现: 设置完 keyDown 回调 + 调 set_focused 后, 按键完全不触发.
- `Widget::onFocusHook` / `onBlurHook` 在 widget.h 已存在, page_state.cpp 已通过
  v-on:focus / v-on:blur 用上, 但**没暴露 C API**. C 端调用方收不到失焦通知,
  写"点击别处自动收起的输入框"做不到.

**修复**:
- `ui_custom_set_focused(w, focused)` 升级 — 仍写 paint bool, 额外通过
  `Context::FindWindowByWidget(cw)` 找 owner window, 调 `SetFocus(cw)` /
  `ClearFocus()`. 旧调用方 (不挂 keyDown 的纯绘图自定义 widget) 完全兼容.
- 新增 `ui_custom_set_focusable(w, int focusable)`: opt-in 让 custom widget 进
  键盘焦点系统. 默认 false (向后兼容). 接管键盘交互的调用方 (例如快捷键输入框,
  自绘文本框) 在创建时置 1.
- 新增 `ui_widget_on_focus(w, cb, ud)` / `ui_widget_on_blur(w, cb, ud)`: 暴露
  `Widget::onFocusHook` / `onBlurHook` 给 C 端. 任意 widget 类型可挂. cb=NULL
  解绑.
- `Context::FindWindowByWidget(Widget*)`: lib 内部新工具. 走 parent 链到 root,
  在 `windows_` 表里反查哪个 window 持有这个 root.

**典型用法** (custom widget 接管键盘交互):
```c
UiWidget w = ui_widget_find_by_id(root, "shortcut_input");
ui_custom_set_focusable(w, 1);                              /* 必须 — opt-in */
ui_custom_on_mouse_down(w, on_click,    self);              /* 点击切焦点 */
ui_custom_on_key_down  (w, on_key,      self);              /* 接管按键 */
ui_widget_on_blur      (w, on_blur,     self);              /* 点别处自动收起 */
```
点击 widget: lib mouse_down 分发自动 `SetFocus(this)` → on_focus 回调 / 视觉
切 focused 态; 按键: 路由到 on_key; 点别处: lib `SetFocus(otherTarget)` → 此
widget 触发 blur 回调.

**ABI**: 4 个新导出 (ui_custom_set_focusable, ui_widget_on_focus,
ui_widget_on_blur, 加上 Context::FindWindowByWidget 内部用). `ui_custom_set_focused`
签名不变, 仅行为扩展 — 旧调用兼容.

## 1.5.0 — build 63

### `ui_debug_server` 支持多 server 并存, 每窗口独立 pipe (L12)

**行为变化**: 同进程内可以为不同 `UiWindow` 同时启动多个 debug server, 每
个绑一个独立 pipe 名. 之前 `ui_debug_server_start` 用 `static ServerState
g_state` 单例, 后启的会被前一个抢占 (单例覆盖 + 旧线程仍跑), 多窗口应用
（主窗口 + 设置窗口）没办法对子窗口独立做 IPC 截图 / tree dump.

**实现**:
```cpp
struct ServerEntry {
    UiWindow              win = 0;
    std::string           pipeName;
    std::thread           thread;
    std::atomic<bool>     shutdown{false};
    HANDLE                pipe  = INVALID_HANDLE_VALUE;
    HANDLE                event = nullptr;
};
static std::vector<std::unique_ptr<ServerEntry>> g_servers;
static std::mutex                                g_serversMu;
```

`ui_debug_server_start(win, pipe_name)`:
- 加锁查重名: 重复 pipe_name 直接返 `-2` (不再静默覆盖)
- 新建 entry, push 进 vector, 用 entry 的稳定堆地址传给 worker 线程
- `userHandler` 仍是全局 (跨 server 共享, 通过 `g_handlerMu` 保护)

`PipeWorker(ServerEntry* self)` 用 `self->*` 而不是 `g_state.*`, 每个 worker
线程操作自己 entry, 互不干扰.

**新增 API**:
```c
UI_API void ui_debug_server_stop_named(const char* pipe_name);
```
按名字停单个 server. 旧 `ui_debug_server_stop()` 行为升级为"停所有".

**停止顺序 (避免死锁)**: 锁内把 vector swap 出来, 锁外再 set shutdown +
SetEvent + join + CloseHandle. 防止 worker 线程 invoke_sync 回主线程时遇
到 stop 调用者持锁等 join.

**典型场景**: GuoheView 主窗口 `guohe_view_debug` + 设置窗口
`guohe_settings_debug` 同时在线, 测试脚本按需选 pipe.

**ABI**: 新增 `ui_debug_server_stop_named` 一个导出. 旧 API
`ui_debug_server_start` / `ui_debug_server_stop` 签名兼容; 旧调用方式
(只启一个 server, 用 stop() 关) 不需要改.

## 1.5.0 — build 62

### `LabelWidget::SizeHint` 返自然 intrinsic 宽, 修 absolute shrink-to-fit 父级死锁 (L9)

**行为变化**: `<label wrap=true>` (lib 默认) 放在 `position: absolute`
shrink-to-fit 父级里时, 父级宽度会按子内容 intrinsic 宽度撑开, 而不是
卡死在第一帧的值. 跟 CSS 2.1 §10.3.7 对齐.

**旧行为 bug**:
```cpp
if (wrap_) {
    if (parent_) {
        float parentW = parent_->rect.right - parent_->rect.left;
        if (parentW > 20.0f) {
            availW = parentW - padL - padR;
            if (w > availW) w = availW;   // 把 w cap 到父 *上一帧* rect
        }
    }
}
```
循环依赖: 父级 SizeHint = 子级 SizeHint.w (蛋); 子级 SizeHint.w = min(
intrinsic, 父上一帧 rect - padding) (鸡). 父宽永远卡在第一次 layout 的值.

**典型症状**: 动态文本场景 — counter / i18n 切换 / status badge —
文本变长后只在原父宽里 wrap 成多行, 父级不撑开. GuoheView 的 nav 计数器
"3 / 14" → 切到 "14 / 14" wrap 成两行就是这个 bug.

**修复**: SizeHint 一律返 intrinsic `w = estW`. 折行实际发生在 DrawText
阶段 (EnsureLayout 用 widget 真实 rect.width 作 layoutW), 不受 SizeHint
影响.

**Wrap height**: 只在 `fixedW > 0` (显式宽度约束) 时 SizeHint 算 wrap 后
行数 + 高度. 没 fixedW 时返单行高. flex shrink 父级 + 无 fixedW 的 wrap
label 仍然在 draw 时按 rect.width 折行, 但 SizeHint 给单行高 — 实际场景
触发面窄, 真正修法 (双趟 layout / 传 availW 给 SizeHint) 留后续大改.

**ABI**: 纯实现, 兼容.

## 1.5.0 — build 61

### `ui_widget_on_mouse_leave` widget 级 mouseleave 回调 (L8)

**新增 API**:
```c
typedef void (*UiWidgetMouseLeaveCallback)(UiWidget w, void* userdata);
UI_API void ui_widget_on_mouse_leave(UiWidget w,
                                      UiWidgetMouseLeaveCallback cb,
                                      void* userdata);
```

任意 widget 类型都可挂的 mouseleave 回调. 两条触发路径:
1. **同窗口 widget→widget 转移**: cursor 从 widget A 移到 widget B 时,
   旧 hovered ancestor 链中不在新链的 widget 逐层 fire leave;
2. **出窗口 (WM_MOUSELEAVE)**: 当前 hovered ancestor 链全员 fire leave.

跟 web `mouseleave` event / CSS `:hover` 退出语义对齐. 无 x/y 参数 —
leave 时 cursor 已脱离, 给坐标无意义.

**为什么需要**: 之前 WM_MOUSELEAVE 内部处理 (清 hover CSS state) 但不暴露
callback; widget→widget 转移也是仅清 CSS state. 想做 "cursor 离开 X 时
关 popover / 收 overlay / 隐 tooltip" 类 hover-driven UX 都没法做. 下游
只能 hack 在多个 sibling widget 上挂 mouse_move 倒推, 出窗口仍然漏。

**实现位置**: `UiWindowImpl::OnMouseMove` 的 hover 切换块 + `case
WM_MOUSELEAVE` 的 hovered chain 清理循环 — 两处都已经在遍历清 hover
状态, 加一行 fire 不增加新遍历开销.

**ABI**: 纯增量, 兼容.

## 1.5.0 — build 60

### `ui_widget_on_mouse_move` 通用鼠标位置回调 (L7)

**新增 API**:
```c
typedef void (*UiWidgetMouseMoveCallback)(UiWidget w, float x, float y, void* userdata);
UI_API void ui_widget_on_mouse_move(UiWidget w,
                                     UiWidgetMouseMoveCallback cb,
                                     void* userdata);
```

任意 widget 类型 (div / svg / ui_gh_img_view / ui_image / ui_custom...) 都
可挂的 mouse_move 回调. 回调 x/y 是 widget-local DIP (相对 widget rect
左上). 调用 cb=NULL 解绑.

**为什么需要**: `ui_image_on_mouse_move` 内部 cast 是 `As<ImageViewWidget>`,
对 sibling widget 类型 (典型: `ui_gh_img_view` 的 `GhImgViewWidget`) 全部
静默 no-op. 下游绑了完全不会发觉, callback 当成 dead code 还以为是平台
bug. 改用 `ui_widget_on_mouse_move` 利用基类 `Widget::onMouseMoveHook`
字段 (ui_window mouse-move dispatch 早已对 hit widget 触发它), 不需要
具体子类配合.

**配套**: `ui_image_on_mouse_move` cast 失败时 `OutputDebugStringA` 一行
warning, 指向替代品, 不再默默失败.

**ABI**: 纯增量, 完全兼容.

## 1.5.0 — build 59

### `ui_window_get_rect_screen` 返 DIP — 修 x/y 单位跟 setter 不对称 (L6)

**Behavior change**: `ui_window_get_rect_screen` 返出的 x/y 现在是 DIP
(以前是物理像素). w/h 之前就已经是 DIP, 没改.

之前 GetWindowRectScreen 实现:
```cpp
if (x) *x = r.left;     // 物理像素
if (wDip) *wDip = (int)((r.right - r.left) / dpiScale_);  // DIP
```
而 setter 路径 (CreateWindow / SetWindowRect / SetWindowPosition) 输入
x/y 都按 DIP 处理 (MulDiv 乘 dpi/96 转物理). 不对称导致应用"存窗口位置
→ 重启恢复"在 DPI != 100% 时位置每次 × DPI scale drift (150% DPI 4 次
后窗口跑出屏幕).

修法: x/y 也除 dpiScale_, 跟 w/h / setter 对齐:
```cpp
if (x)    *x    = (int)(r.left  / dpiScale_);
if (y)    *y    = (int)(r.top   / dpiScale_);
```

**调用方影响**: 唯一可能依赖物理 x/y 的调用方是反向 round-trip 存 / 读
窗口位置 — 但他们本来就 drift, 这次修复反而让他们工作正常. 实际没有
"特意依赖物理 x/y" 的合理调用方.

## 1.5.0 — build 58

### `ui_widget_set_cursor` 程序化光标 API (L5)

新增 C-ABI + enum:

```c
typedef enum { UI_CURSOR_DEFAULT, UI_CURSOR_POINTER, ..., UI_CURSOR_NONE } UiCursor;
UI_API void ui_widget_set_cursor(UiWidget w, int cursor);
UI_API int  ui_widget_get_cursor(UiWidget w);
```

之前 lib 内 `Widget::cursor` (CursorKind enum) + `ui_window.cpp:1080` 的
IDC_HAND/IDC_IBEAM 映射全套机制已就绪, 但只能通过 .uix CSS
`cursor: pointer` (widget_factory.cpp:716-732) 设置, **程序化挂载的
widget 没办法设光标**. 加 C-API 暴露给应用层. enum 数值跟内部
CursorKind 一一对应, 越界值被 setter 忽略, 不会破坏 widget 状态.

向后兼容: 仅新增 API, .uix CSS 路径不动.

## 1.5.0 — build 57

### `ui_icon_button` ghost 模式 hover_visual 开关 (L4)

新增 C-ABI:

```c
UI_API void ui_icon_button_set_hover_visual(UiWidget w, int enabled);
UI_API int  ui_icon_button_get_hover_visual(UiWidget w);
```

之前 ghost 模式 hover/press 时硬编码画 `theme::kBtnHover()` / `kBtnPress()`
背景, 没办法关闭. 这次加 `hoverVisual_` 成员 (默认 true, 行为兼容), 设
false 时 ghost 模式只渲染 icon, 任何状态都不画 bg —— 适合 titlebar
装饰按钮 / 状态指示器等"不希望按钮凸出"的场景.

实现仅改 `IconButtonWidget::OnDraw` ghost 分支, 用 `if (hoverVisual_)`
包住 bg fill 那几行. Normal (非 ghost) 分支不动 —— 那是显式按钮, hover
视觉是核心 feature.

向后兼容: 默认 hoverVisual_=true, 现有代码行为完全不变.

## 1.5.0 — build 56

### `ui_gh_img_view` 加 rotation API (L2 — GuoheView 看图器)

新增两条 C-ABI:

```c
UI_API void ui_gh_img_view_set_rotation(UiWidget w, int angle);  // 任意 int → 0/90/180/270
UI_API int  ui_gh_img_view_get_rotation(UiWidget w);
```

设计选择 (跟 image_view_plus 保持一致的"polished"行为):

- **pan 存屏幕空间, 跟 rotation 解耦**: 鼠标拖动方向永远匹配视觉方向, 旋转
  90° / 180° / 270° 都不破坏 "鼠标向右拖 → 图视觉向右走" 直觉. 这是 fit
  到位 + 正确的 ScreenToImage / ImageToScreen 实现的副产物.
- **Fit (含外部 `ui_gh_img_view_fit`) rotation-aware**: 用 effective W/H
  (90/270 时 fullWidth/Height 互换) 算 zoom, 旋转后视觉 AABB 正好填满
  画布短边.
- **SetZoomAround rotation-aware**: 锚点屏幕→图坐标走完整 inverse rotation
  matrix, zoom 改变后反推 pan 使锚点处图像像素恒定.
- **Tile visibility (DrawLevel + NotifyViewport) rotation-aware**: widget
  rect 4 角通过反旋转回 logical 空间, 取 AABB 算 tile 范围. rotation=0
  时退化为旧路径, 无性能损失.
- **Begin / Clear 重置 rotation_ = 0**: 切新图不继承上一张旋转, 跟 Win11
  照片 / IrfanView 一致.

内部实现:
- `OnDraw` 在 `PushClip` 后用 `D2D::Matrix3x2F::Rotation(angle, dest中心)`
  套一层 transform, preview / 多级 tile 仍画在 logical (未旋转) dest, D2D
  完成视觉旋转. rotation=0 时跳过 GetTransform/SetTransform 开销.
- 新增 `ComputeVisualDestRect()` (effW×effH×zoom AABB) 给 fit / 早期剔除
  / 命中测试用; 原 `ComputeDestRect()` 保留 logical 语义.
- `RotateCW` / `RotateCCW` 整数 90° 步进 (switch/case 精确不带浮点误差).

## 1.5.0 — build 55

### `ui_gh_img_view` 默认背景色：透明（跟其它 widget 行为对齐）

旧行为：构造函数硬编码 `bgColor = {0.10, 0.10, 0.10, 1.0}`（深灰），覆盖
`Widget` 基类默认的 `{0,0,0,0}` 透明值。结果：宿主在 .uix 给父容器写
`background: linear-gradient(...) + #d8d9dc`（看图软件常见的浅灰棋盘格
画布）后，widget 仍画自己的深灰填底，把父容器 CSS 完全遮住。跟其它
core-ui widget（controls / button / label …）默认透明的行为也不一致。

修法：删除构造函数那一行硬编码，`bgColor` 沿用基类默认透明。CSS 解析器
本来就把 `gh_img_view { background: ... }` 写入这个字段，行为现在跟
其它 widget 完全对齐。下游需要自带底色（例如配 dark theme 的 demo）：

```css
gh_img_view { background: #1a1a1a; }
```

`OnDraw` 那一行 `r.FillRect(rect, bgColor)` 不动 —— D2D 对 alpha=0 是
no-op，行为透明，让父容器 CSS 透出来；同时保留 alpha>0 时画自定义底
色的能力。

GuoheView 反馈编号：L1。

## 1.5.0 — build 54

### `ui_gh_img_view` 渲染质量：LINEAR 插值 + 多级金字塔叠加

旧渲染两个问题:
1. 用 `D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR` —— 当 zoom 不是整数
   倍 (例 0.7 / 1.4) 时上采样肉眼可见块状, 用户描述为"模糊"。
2. 只画 active level —— zoom 跨级瞬间, 新 level 瓦块还在异步加载, 期间画
   面只剩 preview (低分辨率拉伸); 已经在 widget 里的旧级瓦块被白白丢弃。

修法:
- 切到 `D2D1_BITMAP_INTERPOLATION_MODE_LINEAR`, 上下采样都平滑。
- OnDraw 改成从最粗 level 到最细遍历, 每级把已加载的可见瓦块绘制。后画
  覆盖先画 → active level 在最上面, 旧级在下面兜底。切级即时无缝。
- 取消 NEAREST 时为防接缝的 ±1 px 外扩 (LINEAR 不需要, 会反而出色边)。

新增私有方法 `GhImgViewWidget::DrawLevel(level, dest)`。

## 1.5.0 — build 53

### `ui_gh_img_view` 鼠标 wheel + 拖拽 capture 路由

`ui_window.cpp` 的 `OnMouseWheel` / `OnMouseDown` / `OnMouseUp` 三处用
`dynamic_cast` 硬编码白名单分发到具体 widget 类型，新加的 `GhImgViewWidget`
不在名单里 —— 滚轮事件根本没到 widget，drag 出窗口后丢 capture 无 pan。

补到三处白名单。验证：滚轮 zoom + 鼠标拖拽 pan 两个都通过 `gh_state` IPC
反馈对得上。

## 1.5.0 — build 52

### `ui_gh_img_view` PickAutoLevel 方向反了

`PickAutoLevel` 旧逻辑选"最大的满足 scale*zoom >= 0.95 的 level"，方向完全
反了。正确语义：选最低（最清晰）的 level 使 `screen_per_level >= 1`（避免
欠采样的同时不浪费纹理）。

旧逻辑表现：
- zoom=2.0（正常放大）选 active_level=6（最低分辨率，应该选 0）
- zoom=0.0366（fit 18641 大图）选 active_level=0（全分辨率，浪费纹理上传，
  应该选 5）

通过 `gh_state` IPC 验证修复。

## 1.5.0 — build 51

### `ui_gh_img_view` IPC 调试支持

`zoom`/`pan` 命令增加对 `ui_gh_img_view` widget 的识别（之前只懂
`ui_image_view_*`，对 gh_img_view 静默 no-op，导致自动化测试假阳性）。

新增 IPC 命令（`examples/gh_img_view/` 自动化测试用）：
- `gh_state <id>` — 返回 widget 状态 JSON：full_w/h、tile_size、levels、
  active_level、auto_level、zoom、pan
- `gh_fit <id>` / `gh_reset <id>` — 触发 Fit / Reset
- `gh_level <id> <lvl>` — 强制切 active level
- `gh_auto_level <id> <0|1>` — 切 auto-level 模式
- `gh_zoom_around <id> <zoom> <ax> <ay>` — 锚点缩放

## 1.5.0 — build 50

### `ui_gh_img_view` — 通用瓦块画布 widget（新组件）

按 gh-img-decode 解码器的输出形状量身设计的画布：BGRA8 premultiplied + 256
瓦块 + 多级 pyramid。不依赖任何外部库，调用方喂数据，widget 负责显示与交互。

新增 API（`ui_core.h`）：

```
ui_gh_img_view()                          /* 创建 widget */
ui_gh_img_view_begin(w, win, &info)       /* 宣告图像数据形状 */
ui_gh_img_view_set_preview(...)           /* 全图缩略图兜底 */
ui_gh_img_view_set_tile(level, tx, ty, …) /* 喂某级某瓦块 */
ui_gh_img_view_clear_level(level)         /* 清单级（切级前用）*/
ui_gh_img_view_clear()                    /* 卸载图 */
ui_gh_img_view_set_auto_level / set_active_level
ui_gh_img_view_set_zoom / set_zoom_around / fit / reset
ui_gh_img_view_on_viewport(cb, ud)        /* 视口反馈 → 喂下一批瓦块 */
```

数据形状契约见 `CLAUDE.md` "ui_gh_img_view widget" 段。集成 demo 在
`examples/gh_img_view/`，独立 CMake 项目，只通过二进制 release 集成
core-ui + ghde 解码器，不允许 add_subdirectory 源码。IPC pipe 独占名
`gh_img_view_demo`，避开 `ui_core_debug` 通用名冲突。

不做（防滑坡）：HDR fp16（v2）、动画多帧、文字/状态栏/装饰、文件 IO。
画布纯净。

## 1.5.0 — build 49

### L22 — `ui_image_view_plus_set_zoom_around` 锚点缩放 API

下游场景：用户 wheel 在画布右上角放大 → 触发 viewport callback → 应用做
reload (切高 pyramid level / 换图)。reload 后应用想保持鼠标锚点对应的图
像位置不动，但 lib 没暴露"以指定锚点缩放"的 API：现有
`ui_image_view_plus_set_zoom` 只换 zoom 不动 pan，应用手动算新 pan 让
canvas center 对齐 → **鼠标在边角时视觉跳跃**。

新 API：
```c
ui_image_view_plus_set_zoom_around(canvas, new_zoom, anchor_x, anchor_y);
```

`anchor_x/y` 是 widget 像素坐标（鼠标位置），算法跟原生 wheel 缩放完全
一致 —— anchor 处的图像像素在 zoom 前后保持不动。下游 reload 完调一次
就行。

实现：抽出 `OnMouseWheel` 内已经写好的锚点缩放算法成
`ImageViewPlusWidget::SetZoomAround(z, anchorX, anchorY)` public method，
`OnMouseWheel` 改用它（不重复实现）；C API `ui_image_view_plus_set_zoom_around`
直接转调。

不动现有 `UiViewportCallback` 签名（`zoom, panX, panY`）—— 加 anchor
字段是 breaking change，下游接的所有回调都得改。新 API 替代足够覆盖
场景。

文档：`docs/controls.md` 加「锚点缩放」章节，含 reload 后保持鼠标锚点
的完整代码示例。

## 1.5.0 — build 48

### L18 — `ImageViewPlus` letterbox 完全透明，应用层用 CSS 装饰

回退 build 47 (L15) 的 OnDraw 顶部 letterbox 填充逻辑（之前是
`checkerboard ? DrawCheckerboard(rect) : bgColor ? FillRect(rect, bgColor) :
FillRect(rect, theme::kContentBg())`）—— 这条路径让 lib 接管了 letterbox
颜色 / 棋盘的渲染权，但 lib 内置样式（白底 + #999/#ccc 棋盘）写死，应用层
改不了。

**新行为**：letterbox 区完全不画任何东西，应用层给父容器 CSS background
（纯色 / 渐变 / 任意花纹）想怎么装饰都行。

`set_checkerboard` API 退化语义：只在图区 (`dest`) + 图带 alpha (PNG 透明
像素) 时画棋盘，给透明像素位置一个对比背景。letterbox 不参与。

**设计哲学**：跟 `<button>` / `<label>` / `<svg>` 等 widget 一致 —— 只画
自己的内容，不画自己的"留白"。CSS 表达力远强于 lib 内置 setter。

L16 pan hit-test 保留（独立改善，pan 区天然只在图上更稳）。

## 1.5.0 — build 47

### L14 — C API 几何 / 树结构 setter 自动 mark layout dirty + invalidate

`ui_widget_add_child` / `remove_child` / `set_width` / `set_height` /
`set_size` / `set_expand` / `set_padding` / `set_padding_uniform` 之前
都只 mutate 字段，不 mark layout dirty 也不 invalidate window。下游用
C API 写"打开图片→换图"等运行时插入逻辑的 100% 撞坑：add_child 一个
带 fixedW/H 的 widget 后看到 rect=[0,0,0,0] 不显示，必须显式
`ui_window_relayout` + `ui_window_invalidate` 才出来。`.uix` 路径走
compiler 自带 layout 调度没事，只有 C API 路径有问题。

修：加 file-local helper `MarkLayoutAndRepaint()` 同时调
`ui::RequestLayout()` + `Ctx().InvalidateAllWindows()`，所有
layout-affecting setter 调用尾插入。

附加诊断：用户怀疑 `ui_widget_set_expand(canvas, 1)` 在 VBox 单子场景没
拉伸是相关 bug。其实是因为同时设了 `fixedH` —— VBox 布局里 `fixedH > 0`
优先于 `expanding`，`expand` 不 apply。这是 CSS `flex-grow` 的标准语义
（flex-grow 只在 flex-basis: auto 时分配剩余空间），不是 bug。

### L15 — `ImageViewPlus` letterbox 区跟随 checkerboard / bgColor

`ImageViewPlusWidget::OnDraw` 之前 `r.FillRect(rect, theme::kContentBg())`
强制全 widget rect 填白色；checkerboard 只在 dest（图区）+ `caps.alpha`
时画。结果：letterbox 永远白色不可定制，`set_bg_color` 也被覆盖。

修：背景按优先级渲染 —— `checkerboard` 开 → 全 widget rect 棋盘（含
letterbox） → 否则 `bgColor` → 最后 theme 兜底。`dest` 区单独再画的
checker 删掉，因为顶层 rect 已经铺过，图透明像素自然透出。

### L16 — `ImageViewPlus` pan 限制在图实际显示矩形内

`ImageViewPlusWidget::OnMouseDown` 之前用 `Contains(e.x, e.y)` 接整个
widget rect 的事件，落 letterbox 也启动拖图。

修：crop mode 之外的 pan 路径计算 `dest` 矩形，`e.x/e.y` 不在 `dest`
内直接 `return false`（不接事件）—— 跟标准看图软件 (Photos / IrfanView /
Honeyview) 行为一致。

### L17 — 不是 bug

用户报"父 background-image (4 层 gradient) + var(--bg-canvas)，child
fill 父，child 区域只见 bgColor 没 gradient"。verify exe 复现后截图证实：
**用户的 0.04 alpha 太低，肉眼难以辨别**。同样的 CSS 把 alpha 改到
0.20 棋盘格清晰可见；L10 fix 的渲染逻辑（bgColor 先画，gradient 叠在
上面）实际正确。建议反馈用户把 alpha 调到 0.08-0.12 区间。

## 1.5.0 — build 46

### L13 — HBox `align-items: center` 真正让 inline 子居中 + CSS display 语义

之前 L3 fix 给 `<label>/<span>/<a>` 默认 `alignSelfOverride=Start` 模拟
inline "不被 stretch" 行为。但按 CSS spec，`align-self: flex-start` 是
atomic 的（同时管 sizing + positioning），导致 L13 case 里 toolbar 的
`align-items: center` 被 label 的 flex-start override —— label 强行顶到
cross-axis 起点，cmd-btn 跟 label 的 cross-center 不在 toolbar 中线上。

实现 CSS `display` 语义把 sizing 和 positioning 拆开：

- `widget.h`：加 `enum class Display { Block, Inline, InlineBlock, None }`
  + `Display display = Display::Block` 字段
- `widget_factory.cpp`：
  * factory 给 label/span/a/small/strong/em 默认 `Display::Inline`
  * BuildWidget 集中块给 button / svg / img / input(text/range/number) /
    textarea / select / toggle / checkbox / radio / progressbar / icon-button /
    caption-button 默认 `Display::InlineBlock`（CSS spec inline-block）
  * 移除 L3 hack `lbl->alignSelfOverride = Start`
  * `ApplyCommonStyle` 解析 CSS `display:` 属性
    (none/inline/inline-block/block) 可 override 默认
- `widget.cpp`：3 处 cross-axis 布局 (VBox / HBox / HBox-wrap) 加判断：
  ```cpp
  bool intrinsic = (display == Inline || InlineBlock) &&
                   alignSelfOverride < 0;
  if (effAlign == Stretch && !intrinsic) stretch;
  else                                   useSizeHint;
  ```
  positioning switch 不变（仍按 effAlign 走 Stretch/Start/Center/End）。

效果对应 CSS spec：

| widget | container align-items | child align-self | 结果 |
|---|---|---|---|
| block (`<div>`) | stretch | auto | stretch ✓ |
| inline (`<label>`) | stretch | auto | **不 stretch**（CSS spec inline 不被 stretch）✓ |
| inline (`<label>`) | center | auto | 不 stretch + 居中 ✓ (L13 修) |
| inline (`<label>`) | stretch | stretch (用户显式) | stretch（opt-in）✓ (L3 opt-in 没回归) |
| inline (`<label>`) | center | flex-start | 不 stretch + 顶部 |

像素验证：toolbar (h=44, align-items:center, y=16..60, center=38) 包 button
(32) + 两个 label (h=22)，三个 child 的 y center 全部精确等于 38。L3
badge "3 / 8" 仍 shrink 到 textW+padding=45px。`align-self: stretch`
opt-in 的 label 仍撑满父内宽。

### 影响：依赖 button / form 控件 auto-stretch 的现有 UI

之前 `<button>OK</button>` 裸放在 44px toolbar 里会被拉成 44 高（违和），
现在按 CSS spec 用 SizeHint=32 高 + 顶部对齐（Stretch positioning）。要恢复
旧行为：父加 `align-items: center` 让按钮居中，或给按钮加
`align-self: stretch` 强制撑满。

## 1.5.0 — build 45

### L12 — `<label>` 响应 CSS padding

`LabelWidget::SizeHint` 之前返回 `{estW, fontSize+10}`，没加 `padL/padR/
padT/padB` —— layout 给的 rect 只跟文字尺寸一样，CSS padding 完全不撑大
（badge / pill / chip 类用 padding 撑出 fluent 风的 medium / large 标签
做不出来）。`OnDraw` 也直接 `DrawText` 进 rect 不减 padding，即使 rect
撑大了文字也会画到 padding 区域。

修：
- `SizeHint` 返回 `{estW + padL + padR, h + padT + padB}`
- `OnDraw` 算 `textRect = rect 减 padding`，所有 `DrawText` 用 `textRect`；
  选区路径的 `x1/x2` 基准从 `rect.left` 改 `textRect.left`；clip 也用 textRect
- `CharIndexAtX` 把 `relX = x - rect.left` 改成 `x - rect.left - padL`，
  让选中点击位置正确映射到字符索引

像素验证：`<label class="badge" padding="6px 16px 5px 16px">3 / 8</label>`
现在 size = `[textW+32, textH+11]`（之前是 `[textW, textH]`，padding 全
被忽略）。

## 1.5.0 — build 44

### L10 — `background-color` 在有 `background-image` 时也生效

两个独立 bug 共同造成「multi-gradient 透明棋盘 + 自定义底色」做不出来：

1. **解析**：`widget_factory` 只读一个 source（`background-image > -color >
   shorthand`），写了 `background-image: ...` 时 `background-color` 完全
   不读；`background:` shorthand 末尾的 bare Color（如 `var(--bg-canvas)`
   解析后）也只把整串当 image 解析，循环里只收 Function 漏掉 Color。
2. **渲染**：`widget.cpp` 的 `if hasBgGradient / else if drawBg` —— 有
   gradient 就完全不画 bgColor。但 CSS spec 是 image 画在 color 之上。

修：
- `widget_factory.cpp` 三种写法独立处理：`background-color` 单独存 bgColor；
  `background-image` / `background` shorthand 按 top-level comma 分 layer，
  每 layer 收 Function → gradient，最后一个 layer 的 bare Color → bgColor
  （仅当 `background-color` 没显式设时）；纯色 `background: red` 仍 fallback
  能 work。
- `widget.cpp` 去掉 `else`，bgColor 先画，gradients 叠在上面。

### L9 — `<div>` 默认 `gap: 4px` 文档化

用户反馈走 CSS 路径（`<div class="...">`）时不知道默认 `gap` 是 4，紧贴
布局（toolbar 紧贴 canvas）必须显式 `gap: 0`，新人踩坑。`docs/layout.md`
的 .ui markup 属性表早就写了 gap 默认 4，但 CSS 章节没提。加一段醒目的
「> 注意」块说清。**不改默认值** —— 改成 0 会回归所有现有 UI。

### release 流程修复（顺手）

- `release-package` 漏依赖 `core-ui-fat-static`：`--target release-package`
  时 fat lib 不会重建，install 拷过去的 `lib/static/core-ui.lib` 是几次
  构建之前的旧版本。CMakeLists.txt 加上依赖。
- `core-ui-static` target 没注入版本宏：`target_compile_definitions(core-ui ...)`
  只给 DLL target 注入 `UI_CORE_VERSION_*`，并行的 `core-ui-static`（fat
  lib 用）没注入 → `ui_core.h` 的 `#ifndef` 默认值 `1.0.0.1` 生效，下游
  静态链接拿到的 `ui_core_version_string()` 永远报错版本。抽出
  `UI_CORE_VERSION_DEFS` 变量同时注入两个 target。

`CLAUDE.md` 新增「发布 Release」章节：4 步流程（bump 3 文件 / CHANGELOG /
一次 PS build / 验证 + commit）+ 反模式列表（别改 PS 脚本加 -Static / 别用
clang-cl-build-static / 别信任 install Up-to-date 输出 / 别跳 CHANGELOG）。

## 1.5.0 — build 43

### CSS 布局 / 渲染 6 个用户反馈 bug

L1 `<label style="text-align:center">` 不生效。`ApplyCommonStyle` 没把
cascade 算出来的 `text-align` 取出来塞给 LabelWidget，draw 时一直用默认
`DWRITE_TEXT_ALIGNMENT_LEADING`。修：解析 `text-align` → `lbl->Align(...)`。

L2 `<button align-items:center justify-content:center><svg/></button>` 里
svg 不居中。`ButtonWidget : public Widget` 不是 flex 容器，children 走
`Widget::DoLayout` 都被堆在 (0,0)。改 `ButtonWidget : public HBoxWidget`，
重写 `DoLayout`：没文字 + 有 child 时走 `HBoxWidget::DoLayout`，文字按钮
保持原 legacy 行为。`ApplyFlexContainerStyle` 的 `dynamic_cast<HBoxWidget*>`
现在也匹配 button → button 的 align-items / gap 等 CSS 属性也生效。

L3 `<label>3 / 8</label>` 默认被父 `align-items: stretch` 撑满 cross-axis
内宽，pill / badge / chip 做不出来。新增 `Widget::alignSelfOverride` 字段
+ CSS `align-self` 解析（auto / flex-start / center / flex-end / stretch）。
3 处 cross-axis 布局点（VBox / HBox / HBox-wrap）优先读 child 的 override，
再回落容器的 `crossAlign_`。Inline 文字标签（label / span / a / small /
strong / em）默认 `alignSelfOverride = Start`（shrink-to-fit），匹配 DOM
inline 行为；要恢复 stretch 写 `align-self: stretch`。
`LabelWidget::SizeHint` 配套：去掉 60px floor，宽度估算优先用
`g_activeRenderer->MeasureTextWidth` 的 DWrite 真实测量，旧 char-count
估算只在 renderer 还没 attach 时兜底（粗体小字估算少 ~20%，wrap=true
时会把单行文字折成两行）。

L4 实际是 L3 的衍生症状（label 撑满主轴 → `justify-content` 没空间 +
文字 left-align）。诊断 exe 跑用户原 GuoheView stylesheet 验证，已被
L1+L3 fixes 解决，不需要单独修。

L5 `position: absolute; top: calc(50% - 20px)` 不工作。`ResolvePx` 只调
`ParseLength`，遇到 `calc()` / `%` 直接返回 0；`posLeft/posTop` 当 px
存，没父尺寸 context。Widget 加 4 个 raw string 字段
`posLeftRaw / posTopRaw / posRightRaw / posBottomRaw`：纯 px / em / rem
仍 eager 解析，% 或 `calc()` 把原始字符串塞进 *Raw 字段。4 处 absolute
布局点调新增的 `ResolveAbsSide(raw, fallback, parentSize)`，layout time
parse + EvalCalc，parentSize 按轴传（L/R 用 container width，T/B 用
container height）。`EvalCalc` 早就实现在 `css/value.cpp`，只是没人调它。

L6 多层 `linear-gradient` 只渲染第一层（factory `break` 在第一个
Function），且 `background-size` / `background-position` 完全没解析 —— 经典
4 层棋盘格图案做不出来。`Widget::BgGradient` 加 `tileW/tileH/posX/posY`
字段；`Widget` 用 `std::vector<BgGradient> bgGradients` 替代单 struct。
factory 循环收集所有 gradient component，按 CSS 规则 cycle short list 解析
`background-size` / `background-position` 的 comma-list per-layer。
渲染：无 tile 走老路径全 rect；有 tile 时
`CreateCompatibleRenderTarget` 渲染 1 个 tile 到位图，然后
`BitmapBrush + EXTEND_MODE_WRAP` 让 D2D 自动平铺，`Matrix3x2F::Translation`
应用 `posX/posY`。

### 顺手修的 2 个隐形 bug

reactive recompute 漏调 `ApplyFlexContainerStyle` / `ApplyFlexItemStyle`：
`:class` 切换时 `gap / justify-content / align-items / flex-wrap /
flex-grow / flex` 都不更新（只 reapply ApplyCommonStyle）。修：reset 这
些字段到默认（VBox/HBox 的 gap=0 / mainJustify=Start / crossAlign=Stretch
/ flexWrap=false；widget 自身 expanding=false / flex=1.0），然后顺序调
ApplyCommonStyle → ApplyFlexContainerStyle → ApplyFlexItemStyle。容器类
型 (VBox vs HBox) 仍因 factory 阶段已锁定无法运行时切换。

`ui_debug_dump_tree` 的 `appendTextMetrics` 给 Label / Button / CheckBox
/ RadioButton / Toggle 调用时硬编码 `theme::kFontSizeNormal=14`。CSS 设了
别的 font-size（如 `.badge label { font-size: 12px }`）时，dump 输出的
textWidth 按 14px 测，跟实际渲染宽不符；`lines` 字段也跟着算错（误显示
wrap-to-2-lines，但实际单行就够）。改成读 `w->css.fontSize`，>0 用，
否则回退 14。

### 其他

删除 `demo/golden_runner.cpp` + `demo/golden/` 全套 22 个视觉回归测试
fixture。UI 库已基本成型，回归测试套件不再需要每次跑；后续视觉验证用
独立编译的一次性 exe + 截图人工检查即可。CMakeLists.txt 的
`add_executable(golden_runner)` 块、.gitignore 中 demo/golden/* 条目、
ui_context.cpp 注释中的 golden_runner 举例都同步清理。

## 1.5.0 — build 42

### v-for > v-if > v-for（三级嵌套循环 scope）

build 41 留的 TODO：v-for 行内 v-if 子树里再放一层 v-for，外层迭代变量
（`grp` / `gi`）在内层 listFn / 每行 binding / 每行 event 里都拿不到 ——
内层闭包是用单层 `(loopVar, indexVar)` 编出来的，外层 scope 完全断了。

### 修复

- `JsLoopRuntime` 加 `outerIter / outerParamNames / outerLocals /
  outerHasIdx`：可选的外层 v-for scope 链。
- 新增 `BuildJsLoopRuntimeInScope(spec, outerLocals, outerParams,
  outerIter, hasIdx)`：listFn / keyFn 用 vector<string> 多参数 overload
  编译，调用时把外层 (item, idx) 拼到参数前。
- `RebuildJsLoop / ComputeIterationKey / RewireIterationBindings /
  BuildJsIteration` 的 effect lambda 全部 capture `&rt`，按需把外层 args
  prepend 到 JS_Call 参数表。
- `BuildJsIteration` 编译每行 binding / event 时，paramNames =
  `outerParamNames + [thisLoopVar, thisIndexVar]`，rewriter locals 也合并。
- 新增 `WireLoopScopeEventEx(target, ev, fn, iter, hasIdx, rt)`：处理嵌套
  scope 的事件分派（旧 `WireLoopScopeEvent` 内部转调）。
- `CondInIter` 加 `innerLoopSpecs / innerLoops`；mount path 把
  `sub.loops` move 到 innerLoopSpecs，每个 spec 用 `BuildJsLoopRuntimeInScope`
  建 runtime，外层 scope = 当前 v-for 行的 (loopVar, indexVar, iterRaw)。
- `DestroyJsIteration` / CondInIter unmount 都 cascade tear down 内嵌
  v-for runtime（先于 v-if，避免 stale itemValue ref）。

### 测试

新增 golden `demo/golden/20_vfor_in_vif_in_vfor.uix`：3 组数据，每组带
`show` flag；`show=true` 时进入内层 v-for 迭代每个 item，文字同时引用
外层 `grp.title / gi` 与内层 `item.name / i`。已 bootstrap baseline，渲染
符合预期：Veggies 组（show=false）只显示标题，其他两组完整展开。

## 1.5.0 — build 41

### v-for > v-if 内嵌套 v-if + menus 支持

`BuildJsIteration` 内的 `CondInIter` mount 之前只处理 `sub.bindings +
sub.events` —— v-for 行内 v-if 子树里再嵌一层 v-if 完全不工作（widget
不挂），`<menu>` 也不会注册到 trigger。

修复：把 cond-in-iter 创建逻辑提取成 `PageState::BuildCondInIter`
helper，mount path 加：

- `sub.menus` → `WireSubtreeMenus(sub.menus, menus_)`
- `sub.conditionals` → 递归调 `BuildCondInIter`，结果 push 到外层
  CondInIter 的新 `innerConditionals` 字段

CondInIter 加 `innerConditionals` 字段；unmount 路径递归 tear down 内嵌
cond runtime（dispose effect、free closure、reset mounted）。
`DestroyJsIteration` 也走递归 teardown，避免 v-for 行重建时 stale。

仍未处理：v-for 内 v-if 内 v-for（v-for in v-if in v-for，三级 loop scope
chain），v-model in v-if in v-for（loop scope 写回需要分析每行 item 结构）。
这俩是更深的嵌套场景，留 TODO。

新增 golden `demo/golden/19_vfor_vif_nested.uix` 验证 v-for + 双层 v-if。

## 1.5.0 — build 40

### widget 直接 setter 触发 layout / invalidate

`LabelWidget::SetText` / `ButtonWidget::SetText` / `ButtonWidget::SetIcon`
/ `CheckBoxWidget::SetText` / `RadioButtonWidget::SetText` /
`ToggleWidget::SetText` 之前是 pure field write — 不调 `RequestLayout`，
也不 invalidate。从 .uix 走 `ApplyBindingToWidget` 路径有 `InvalidateOnExit`
RAII 兜底，但**直接 C API 调**（比如 `ui_label_set_text` / `ui_button_set_text`）
绕开 RAII，widget 的 `SizeHint` 依赖文字长度做 auto-fit，文字改了 layout
没重算 → 宽度 stale，文字裁切或越界。

`ButtonWidget::SetTextColor` / `SetCustomBgColor` 同样不 invalidate，颜色
变了下次 paint 才生效（要等无关事件触发）。

修复：所有上述 setter 在 inline 体内调 `ui::RequestLayout()`（影响 layout
的）或 `ui::GetContext().InvalidateAllWindows()`（仅颜色）。controls.h 加
`#include "ui_context.h"` 拉 `GetContext` 声明。

## 1.5.0 — build 39

### page 销毁时 dispatch unmount hooks

`PageState::DetachQuickJS`（page 销毁、window 关闭、page swap 入口）一直
没调 `DispatchUnmountHooks(root)`。结果：用户用
`ui_page_on_widget_unmount` 注册的 cleanup 回调，只在 v-if/v-for 拆除路径
fire，window 关闭时永远不 fire — 注册了"释放外部资源"的代码会泄漏。

修复：detach 入口先 dispatch 一次 unmount，然后再走原本的 effect /
closure / runtime 清理。

## 1.5.0 — build 38

### `ApplyBindingToWidget` 补一批漏分发的 prop

`prop ==` 表是手写 switch，之前漏了不少模板里能写但运行时没分发的 prop：

| prop | 之前 | 现在 |
|---|---|---|
| `:enabled` | 只翻 `w->enabled` | 翻字段 + 全树 `recomputeStyle` 让 `:disabled` 选择器重新匹配 |
| `:min-width` / `:max-width` / `:min-height` / `:max-height` | 完全不分发 | 直接 set widget 字段 + `RequestLayout` |
| `:src` | 完全不分发 | `ImageWidget::SetSrc` + `RequestLayout` |
| `:icon` | 完全不分发 | `ButtonWidget::SetIcon` |
| `:wrap` | 完全不分发 | `LabelWidget::SetWrap` + `RequestLayout` |

之前 `<button :enabled="canSubmit">` 切 false 后 button 颜色仍是 enabled
状态，要等下次 mouse-move 之类的事件才刷颜色，就是 `:disabled` 选择器没
再次 match 导致的。

仍未分发（需要新的 setter 或 layout 解析路径，后续补）：
- `:placeholder` / `:readonly` / `:max-length`（TextInput / TextArea 没对应 setter）
- `:min` / `:max` / `:step`（Slider / NumberBox 范围重设需要 clamp 已有 value）
- `:font-size` / `:bold` / `:align`（Label 字段直接 set + relayout，但要重 measure）
- `:gap` / `:padding`（容器字段，padding 是 4 个分量字符串解析）

## 1.5.0 — build 37

### `<select v-model>` 不回写

`WireQuickJSModelWrite` 的 ComboBox 分支挂的是 `target->onFloatChanged`，
但 `ComboBoxWidget` 用户选择时实际触发的是 `onSelectionChanged`（int
索引）。`onFloatChanged` 永远不 fire，结果 `<select v-model="theme">` 选项
变了 JS 状态永远不更新。

修复：改用 `combo->onSelectionChanged`，回调里把索引写进 JS state（int
保留为 int，不再绕一道 float）。

## 1.5.0 — build 36

### CheckBox / RadioButton 反应式 binding 不再杀动画

`ApplyBindingToWidget` 的 `:checked` / `:selected` 分支走 `SetCheckedImmediate`
/ `SetSelectedImmediate`，把 `animProgress_` 直接 snap 到目标。表现：用户点
checkbox → `OnMouseUp` 调 `SetChecked`（启动动画）→ `onValueChanged` 触发 JS
state 更新 → reactive system 把新值 push 回 widget → `SetCheckedImmediate` 把
正在播的动画立即截断。Toggle 同样的 bug 早就靠 `SetOnFromBinding`（带
`boundOnce_` 标志）修了，这条没跟。

修复：CheckBox / RadioButton 加 `SetCheckedFromBinding` / `SetSelectedFromBinding`
helper（首次绑定时 snap，后续绑定走带动画的 setter，避免 round-trip 杀
动画）；`ApplyBindingToWidget` 的 `:checked` / `:selected` / `:value` 路径
切到 FromBinding。

## 1.5.0 — build 35

### `@mousedown / @mouseup / @mousemove / @wheel / @dblclick` 在所有 widget 上派发

之前 `Widget::OnMouseXxx`（base class）调 `onMouseXxxHook`（绑给 `@event`），
但绝大多数 widget 子类（Button / CheckBox / RadioButton / Toggle / IconButton /
Slider / ComboBox / TextInput / TextArea / NumberBox / NavItem / Expander /
Splitter / TabControl / ImageView 等）重载 `OnMouseXxx` 不调 base class，
hook 永远不 fire。表现：在这些 widget 上写 `@mousedown` / `@mouseup` /
`@mousemove` / `@wheel` 完全没用（之前已发现 `LabelWidget` 同样 bug 并修了，
其它没跟）。

修复：把 hook 派发从 base class 移到 `ui_window.cpp` 的 4 个 dispatch 站点
（`OnMouseDown / OnMouseUp / OnMouseMove / OnMouseDoubleClick / OnMouseWheel`），
hit-test 拿到 widget 后立刻 `if (hit->onXxxHook) hit->onXxxHook(e);` 再调
`hit->OnMouseXxx(e)`。base class 的 hook fire 路径同时移除（避免子类调
base 时 double fire）。所有 widget 的 mouse event hook 现在统一从 dispatch
路径触发，跟子类是否 fall-through 无关。

## 1.5.0 — build 34

### `ui_page_on_widget_mount` 注册时不补触发初始 mount

文档说 mount hook "fires every time a widget is mounted: initial render,
v-if truthy, v-for build"。但实际代码只在 v-if/v-for 那两条路径调
`DispatchMountHooks`；初始 page mount 时调用的 `DispatchMountHooks` 跑在
`AttachQuickJS` 末尾，那时 `lifecycleHooks_` 还是空的（注册必须经
`ui_page_on_widget_mount` C API，调用顺序总是
`ui_page_load_* → ui_page_on_widget_mount → ui_page_open_window`，attach 时
hook 还没注册）—— 结果初始 mount 一次都不 fire，跟文档不一致。

修复：`PageState::OnWidgetMount` 注册时，如果对应 id 的 widget 当前已经
在 root 树里（最常见情况），立刻 fire 一次。语义上等价于 Vue 3
`watchEffect(immediate)` 的 "is currently mounted, here it is"。后续 v-if /
v-for 的 mount/unmount 路径不变。

## 1.5.0 — build 33

### `:visible="<bool>"` 反应式不重新布局

`ApplyBindingToWidget` 的 `prop == "visible"` 分支只设 `w->visible`，没调
`ui::RequestLayout()`。flex 容器的 `DoLayout` 在 `for (auto& child : children_)
{ if (!child->visible) continue; ... }` 跳过隐藏 widget — 所以一个 widget
首次以 `visible=false` 渲染时拿到 `rect = {0,0,0,0}`；后来 `:visible="flag"`
反应式 binding 把 `visible` 翻成 `true`，但没有人触发重 layout，rect 仍是
零，widget 看上去仍不可见。

修复：visible 翻转时调 `ui::RequestLayout()`，下一帧 layout 重跑就会给
widget 正确的 rect。其它反应式属性（`:width` / `:height` / `:opacity` /
`:bg-color` …）的 setter 早就调过 `RequestLayout()`，只有 visible 漏了。

## 1.5.0 — build 32

### LabelWidget 事件 hook 全派发

`LabelWidget::OnMouseDown / OnMouseMove / OnMouseUp` 在 `selectable=false`
（默认）时直接 `return false`，不调用 base `Widget::OnMouseXxx` —— 结果
`onMouseDownHook` / `onMouseUpHook` / `onMouseMoveHook` 永远不触发。表现是
`<label @mousedown="..." @mouseup="..." @mousemove="...">` 这三个事件在
label 上完全失效（`@click` 仍 work，因为 onClick 走 ui_window 的事件冒泡
路径，不依赖 hook）。

修复：三个 override 在 not-selectable 路径 fall-through 调 base class，
selectable 路径在自己的 selection 逻辑跑完后也调一次 base class —— 文本
选区拖拽和用户 `@mousedown` 共存。其它控件不受影响。

## 1.5.0 — build 31

### CSS 主题变量系统（系统级深浅色）

`compiler.cpp::CollectVarsWithTheme` 现在注入 ~30 个语义 token 作为 CSS 变量，
跟 `theme::Current()` 同步。`.uix` 直接写 `background: var(--bg)` /
`color: var(--fg)` 即可，调 `ui_theme_set_mode(UI_THEME_DARK / UI_THEME_LIGHT)`
全部页面自动重 cascade。

注入的 token 分类：

- **品牌**：`--accent / --accent-hover / --accent-press / --accent-text / --accent-selected`
- **表面**：`--window-bg / --window-border / --bg / --bg-2 / --bg-3 / --bg-4`
- **文字**：`--fg / --fg-2 / --fg-3 / --fg-4 / --fg-on-accent`
- **边框**：`--border / --border-subtle`
- **侧栏**：`--sidebar-bg / --sidebar-text / --sidebar-hover`
- **输入**：`--input-bg / --input-border / --input-border-hover / --input-border-focus`
- **卡片**：`--card-bg / --card-border`
- **按钮**：`--btn-bg / --btn-hover / --btn-press / --btn-text`
- **禁用**：`--disabled-bg / --disabled-text`

`:root { --x: ... }` 用户显式定义优先于库默认。

实施：

- `CompiledPage::cssVars` 改 `shared_ptr<vector<...>>`，所有 widget recomputeStyle
  lambda 共享同一份 var 表
- `ui::page::RebuildThemeVars(vars)` 原地重写库 owned key
- `PageState::RefreshThemeStyles()` rebuild + 全树 recomputeStyle
- `ui::page::RefreshAllPageThemes()` 遍历 PageRegistry
- `ui_theme_set_mode / set_accent / set_accent_hex` 自动触发 refresh
- v-if/v-for 子模板新增 `parentVars` 参数，共享主页 var 表
- `compiler.cpp::FormatHex` 输出 8-char hex `#rrggbbaa` 当 alpha < 1（修一个
  半透明 token 序列化丢 alpha 的 bug —— `sidebarItemHover` dark 模式
  rgba(255,255,255,0.08) 之前被序列化成 `#ffffff`，hover 显示纯白）

### SVG `currentColor` fallback 跟主题

`svg_widget.cpp::inheritFg` 走到父链顶仍未找到 `color` 时，fallback 从
`{0,0,0,1}` 改为 `theme::Current().foreground1`。`<svg fill="currentColor">`
没显式 `color` 也跟主题切换。

### Toggle / ProgressBar 默认动画 + 初始挂载不动画

`ToggleWidget`：

- 加 `boundOnce_` 标志 + `SetOnFromBinding(v)` —— 第一次绑定调 `SetOnImmediate`
  （不动画），后续调 `SetOn`（动画）；初始挂载就停在初始状态
- `OnDraw` 每帧调 `UpdateCachedColors()` —— 之前 cache 在构造函数初始化后**永不更新**
  （死代码），现在能跟 dark mode 切换 + 反映 `UpdateCachedColors` 里设的颜色
- 关闭态颜色：track `#F8F7F7`、thumb `#5B5B5B`、border `#D8D7D7`（同色系扩散）
- 200 ms `EaseOutCubic` 默认过渡，`SetOn` 保留 `animProgress_` 支持中途反向
  打断
- `Context::UpdateAnimTimers()` —— `ApplyBindingToWidget` 的 RAII guard 触发，
  让程序化 state 变化（不在事件回调里）也能启动动画 timer

`ProgressBarWidget`：

- 同样加 `SetValueFromBinding(v)`：第一次绑定 snap，后续 animate
- `<progressbar>` 全小写标签别名（之前只识别 `ProgressBar` / `progress-bar`）
- `:value` 绑定路由到 `pb->SetValueFromBinding()`（之前 `ApplyBindingToWidget`
  的 value 分支没分发 ProgressBar，绑定值丢失）
- 6 px / 6 px track + fill（之前 1 px / 3 px 几乎不可见）

`ApplyBindingToWidget` 里的 `prop == "selected/checked/on"` 和 `prop == "value"`
对 Toggle 都改走 `SetOnFromBinding`。Checkbox / Radio 仍 Immediate（场景不同）。

### v-for > v-if 子节点修复

`BuildJsIteration` 之前只处理 `subPage.bindings/events`，**完全忽略
`subPage.conditionals`** —— v-for 子节点上的任何 v-if（包括 `v-if="true"` 字面量）
都不会创建 widget。

修复：加 `CondInIter` per-iteration v-if runtime（独立于 page 级 `JsCondRuntime`）：

- 表达式编译走 `CompileLoopBindingClosure(loopVar, indexVar)`，能引用 `it.flag`
  / 索引变量
- mount 子树里的 binding/event 也走 loop closure 注入 `(item, idx[, $event])`
- mount 后跑 `recomputeStyle` 让动态 class 级联 + `RebindWidgetCallbacks`
  + `DispatchMountHooks`
- unmount 时先 dispatch unmount hooks，再清 effects/closures

`page_state.cpp::WireLoopScopeEvent` 把事件 wiring 提取成 helper（click /
change / input / dblclick），`BuildJsIteration` 主路径和 v-if mount 路径都调它。

新增 golden test `demo/golden/17_vfor_vif.uix` 覆盖 `v-if="true"` /
`v-if="it.flag"` / `v-if="!it.flag"` 三类典型表达式。

### 持久化 C 回调 + Vue 风格生命周期 hook（A+B）

C 端 `ui_widget_on_click(w, cb, ud)` 之前直接 set `widget->onClick`，handler 跟
**实例**走；v-if/v-for 销毁 widget → handler 没了。两条路径解决：

**A. id 持久化注册表**（懒人路径，跟 Vue 不一致但能跑）：

- `Context::widgetCallbacks_: unordered_map<string, WidgetCallbacks>` —— 按
  HTML id 存 `onClick / onValueChanged / onTextChanged / onFloatChanged`
- `ui_widget_on_click / ui_checkbox_on_changed / ui_slider_on_changed /
  ui_toggle_on_changed` 设置 widget callback 时同步写到注册表
- `Context::RebindWidgetCallbacks(root)` —— v-if/v-for mount 后遍历子树按 id
  回填到新实例

**B. Lifecycle hooks**（Vue parity 路径）：

```c
typedef void (*UiWidgetLifecycleCallback)(UiPage page, UiWidget w, void* ud);
ui_page_on_widget_mount(page, "btn_x", on_mount, ud);
ui_page_on_widget_unmount(page, "btn_x", on_unmount, ud);
```

调用时机：初始 page mount、v-if truthy/falsy、v-for iteration build/destroy。
传给 callback 的 `UiWidget` 是新实例的 handle（HandleTable 自动 reuse 或 insert）。

`PageState::OnWidgetMount/Unmount` 注册，`DispatchMountHooks/UnmountHooks(root)`
派发。两条路径独立，下游可只用 A、只用 B、或混用。

### v-if 子树挂载后修 cascade + 注册菜单

之前 v-if 重新挂载时，子树 widget 的级联只看编译期父链；`shell.dark` 这类
运行时 `:class` 变化在新子树不生效。

修复：`WireQuickJSConditionals` 子树 mount 后递归调 `recomputeStyle` 让每
widget 重建活体 MatchNode 链；同时调 `WireSubtreeMenus(sub.menus, ...)` 让
v-if 子树里的 `<menu>` 真正注册到 trigger（之前编译产物没人接）。

### Menu 渲染重写

DirectComposition + 透明 swap chain：

- `Renderer::CreateRenderTargetForLayered(HWND)` —— `CreateSwapChainForComposition`
  + `IDCompositionDevice::CreateTargetForHwnd` + `IDCompositionVisual::SetContent`
  把透明 swap chain 上屏到 hwnd（Win10 上 `DWMWA_WINDOW_CORNER_PREFERENCE`
  是 no-op，必须自己合成）
- 链 `dcomp.lib`，include `<dcomp.h>`
- DComp object 以 `IUnknown ComPtr` 持有保活
- popup hwnd 加 `WS_EX_NOREDIRECTIONBITMAP`，去掉 `WS_THICKFRAME` /
  `DwmExtendFrameIntoClientArea`

外观（自 1.5.0）：

- 卡片：light 纯白，dark `#2C2C2C`（跟主题切换）
- 圆角：10 px，D2D 抗锯齿
- 软投影：12 圈半透明 round-rect 叠加，`peakAlpha=0.012`、`verticalY=2`
- 行高 30 / 字体 13 / 图标 16 / kPadding 6 / kIconLeftInset 12 / kMinWidth 180
- hover：light 黑 6%、dark 白 10%
- 分隔线：`theme::kDividerSubtle()` edge-to-edge
- 文字强制 GRAYSCALE 抗锯齿（透明 surface 上 ClearType 会出 RGB 亚像素彩边）

新增 `theme::kForeground1/2/3/4 / kDividerSubtle` 访问器。

### 静态库下游不能链接修复

发布的 `lib/static/core-ui.lib` 是 core-ui 自己的 .obj 归档，**没合并 qjs.lib**。
下游静态链接报 `JS_AtomToCStringLen / JS_GetContextOpaque` 等几十个 unresolved。

修复：CMake 加 `core-ui-fat-static` custom target —— 用 `${CMAKE_AR}`（clang-cl
选 `llvm-lib.exe`，MSVC 选 `lib.exe`）合并 `core-ui-static.lib + qjs.lib` 成
`core-ui-fat.lib`（9.7 MB），install 时 rename 为 `lib/static/core-ui.lib`。

`cmake/UiCoreHelpers.cmake` 加 `ui_core_link_static(target, libpath)` —— 一行
接入：补 `UI_CORE_STATIC` 宏（避免 `dllimport`）+ 系统库 + 静态归档。

### Demo / 项目大清理

- 删 17 个 .cpp + 11 个 .uix（老 markup demo / font / image / 9 个 quickjs_*
  / tabs / div_paint / dialog_long / crash repro）
- `CMakeLists.txt` 去 19 个 `add_executable` + `sync-demo-assets` custom target
  + `ui-demo` version resource + 所有 `install demo/...` 规则
- `release/` 删老归档 v1.1.0 / v1.2.0
- `release/core-ui-v1.5.0/` 不再带 `demo/ sample/`，只剩 `include/ lib/ docs/
  cmake/` + 3 个 .md（15 → 13 MB）
- `scripts/debug-smoke.ps1` / `debug-smoke-uix.ps1` 删

保留：`demo/ui_demo.uix` + `ui_demo_uix.cpp` + `golden/` + `golden_runner.cpp`
+ `lang/ui_demo_*.lang`。

## 1.5.0 — build 30 (BREAKING)

### Page 子系统改用 QuickJS（删 legacy AST 路径）

`<script>` 块从手写 DSL（`data: {…}` shorthand、`#` 注释、`name: stmt` 方法简写）
切到 Vue 3 SFC（`export default { data() { return …; }, methods: {…}, computed: {…} }`），
QuickJS-NG v0.14.0 做求值。`@vue/reactivity` 风格的 Proxy + WatchEffect 重写整套
反应式系统。

新加（QuickJS path）：
- `methods{}` / `computed{}` lazy memo / `v-for` per-iteration scope
- `$t(key)` / `$locale` 反应式 i18n
- `@mousedown` / `@mousemove` / `@mouseup` 事件，`$event = { x, y, delta, button }`
- 自动 dispatch：`export default {…}` 走 QuickJS

删掉的 C API：
- `ui_page_set_handler` / `ui_page_set_handler_ex` —— 用 Vue 3 `methods{}` 替代

删掉的源码 / target（~5k LOC + 13 个 demo）：
- `src/ui/expression/{parser,evaluator,ast}.{h,cpp}` —— 旧 AST 解析器/求值器
- `src/ui/page/{state,state_stmt}.{h,cpp}` —— 旧 `ParseState` + Stmt 执行器
- `PageState::AttachLegacy` + `WireEvents` / `WireConditionals` / `WireLoops` /
  `WireComponents` / `RebuildLoop` / `BuildIteration` 等 ~1200 行
- 整个 component 系统（`<import>` / `ComponentDef` / `LoadComponentDef`）
- 13 个 legacy demo .cpp + .uix（`styles_demo` / `tabs_pure` / `tabs_html_only` /
  `tabs_fancy` / `element_demo` / `reactive_ex_demo` / `bug2_repro` / `demo-uix` /
  `ui-demo-uix` / `app.uix` / `ui_demo.uix` / `components/*.uix` / `element/form.uix`）
- 8 个测试（`expression_test` / `state_test` / `template_parser_test` /
  `sfc_parser_test` / `markup_test` / `layout_test` / `script_runtime_test` /
  `quickjs_smoke_test` / `heap_optimize_test`，后三个是 pre-existing bug）

迁移指南：

| 旧（legacy DSL） | 新（Vue 3 SFC） |
|---|---|
| `data: { count: 0 }` | `export default { data() { return { count: 0 }; } }` |
| `methods: { inc: count = count + 1 }` | `methods: { inc() { this.count++; } }` |
| `# comment` | `// comment` |
| `<import src="..." as="X"/>` | （已删，需手动 inline 单文件） |

### `.html` → `.uix` 单文件组件

Page 子系统从 "伪 SFC HTML" 切到 Vue 风格 `.uix` 单文件组件。文件后缀、文件结构、
解析路径全部迁移；C API 不变。

**File format**：

旧格式（`.html`）：
```html
<!DOCTYPE html>
<window title="X" .../>
<state>
  data: { ... }, methods: { ... }
</state>
<style>...</style>
<div class="root">
  <!-- 模板裸放，没有外层 wrapper -->
</div>
```

新格式（`.uix`）：
```vue
<window title="X" .../>
<script>
  data: { ... }, methods: { ... }
</script>
<style>...</style>
<template>
  <div class="root">
    <!-- 模板必须包在 <template> 里 -->
  </div>
</template>
```

**关键变化**：

- 文件后缀 `.html` → `.uix`（彻底放弃 `.html`，无兼容回退）
- `<state>` 块改名 `<script>`（语法不变，data/computed/methods/props 一致）
- 模板内容必须用 `<template>...</template>` 包起来
- Component 文件**不能**有 `<window>` 标签；page 文件**必须**有 `<window>`
- Component 文件**必须**有 `<template>` 块

**目录 / namespace 重命名**：

- `src/ui/html/` → `src/ui/uix/`
- `html_ast.h` / `html_parser.{h,cpp}` → `template_ast.h` / `template_parser.{h,cpp}`
- `namespace ui::html` → `namespace ui::uix`
- `ui::html::ParseHtml()` → `ui::uix::ParseTemplate()`
- 新增 `src/ui/uix/sfc_parser.{h,cpp}` —— 顶层块切分器（识别
  `<window>` / `<template>` / `<script>` / `<style>` / `<import>` / `<link>`），
  替换原先 `page_api.cpp` 里 5 个手写 `Extract*` 函数

**已迁移文件**：14 个 page demo + 3 个 component + 5 个 element/* 子 demo +
16 个 golden 测试。所有 demo `.cpp` 加载路径同步更新（`L"demo/app.html"` →
`L"demo/app.uix"`）。

**CMake**：

- `ui-demo-html` target → `ui-demo-uix`，`html-demo` target → `demo-uix`
- `demo/html_demo.cpp` → `demo/demo_uix.cpp`
- `scripts/debug-smoke-html.ps1` → `scripts/debug-smoke-uix.ps1`
- 所有 `*.html` glob 改 `*.uix`
- install 路径 `sample/html/` → `sample/uix/`
- `ui_core_embed_text(... demo/ui_demo.html ...)` 同步改 `.uix`

**C API**：完全不变。`ui_page_load_file(L"foo.uix")` 跟之前 `L"foo.html"` 等价。
`UiPage` / `ui_page_*` 函数名都不带后缀字样。

**测试**：

- 新增 `test/src/sfc_parser_test.cpp`（17 个 case：完整 page、component、嵌套
  template、self-closing template、imports、link、多 style、duplicate、
  unknown top-level tag 等）
- `html_parser_test` → `template_parser_test`（沿用旧 case，namespace + 函数名
  跟着改）

**Why**：原来 `.html` 是"用 HTML 文件结构装 Vue 模板"，每加一种顶层块都要在
`page_api.cpp` 里再写一个 `ExtractXxx` 字符串状态机。切到 SFC 之后所有顶层块由
单一 `sfc_parser` 处理，加新块（比如 `<i18n>` / `<script setup>` / 资源 hint）
直接挂上去；`<state>` → `<script>` 也为以后塞 JS-like setup 留路。

文档：[docs/uix-guide.md](docs/uix-guide.md)（原 `html-page-ai-guide.md` 改名 +
重写）覆盖完整语法 + 标准模板 + 错误检查。

---

## 1.4.0 — build 29

### `ui_theme_set_accent_hex` — 字符串色值

build 28 的 `ui_theme_set_accent(UiColor)` 要传 `{r,g,b,a} 0..1` float, 用户
要手动从 hex 算; 加字符串版本走 CSS parser:

```c
ui_theme_set_accent_hex("#2563EB");          /* 6 位 hex */
ui_theme_set_accent_hex("#f80");             /* 短格式 #RGB → #FF8800 */
ui_theme_set_accent_hex("#218554FF");        /* 带 alpha */
ui_theme_set_accent_hex("rgb(37, 99, 235)"); /* CSS rgb() */
ui_theme_set_accent_hex("rgba(37,99,235,1)");
ui_theme_set_accent_hex("red");              /* 命名色 (subset) */
ui_theme_set_accent_hex(NULL);               /* 取消覆盖 */
ui_theme_set_accent_hex("");                 /* 同上 */
ui_theme_set_accent_hex("none");             /* 同上 */
```

返回值: `0` 成功, `-1` 解析失败 (state 不动). 调用方可以 fallback. 走的是
现有 `ui::css::ParseColor` (跟 HTML/CSS 同款解析), 跟内联 `style="color: ..."`
完全一致.

### 验证

`build/theme-test/main.cpp`: 7 张截图覆盖 hex/RGB/rgb()/命名色/NULL/无效输入.
- `result_brand_green.png`: `#218554` 绿
- `result_short_orange.png`: `#f80` 橙
- `result_rgb_purple.png`: `rgb(127, 64, 179)` 紫
- `result_after_invalid.png`: `not-a-color` 失败, 回默认蓝 (前一次 NULL reset 的)

注意: 命名色是子集 (`red/green/blue/...` 共 22 个常用), CSS spec 的 140 个
extended 颜色 (`crimson` / `dodgerblue` / ...) **不在表里**, 用 hex 写就行.

## 1.4.0 — build 28

### `ui_theme_set_accent` 自定义品牌色

之前主题色硬编码 (Win11 蓝 `#1f6feb`), 没法换品牌色. 加 C API:

```c
UiColor green = {0.13f, 0.52f, 0.32f, 1.0f};   // #218554
ui_theme_set_accent(green);
ui_window_invalidate(win);   // 立即重绘

// 取消覆盖 → 回当前 mode 默认 accent
ui_theme_set_accent({0,0,0,0});  // alpha=0
```

#### 派生规则

单一 base 色派生 5 个变体, 调用方不用算:

| Token | 派生 |
|---|---|
| `accent` | base |
| `accentHover` | base 各通道 +0.08 (轻提亮) |
| `accentPress` | base 各通道 -0.12 (压暗) |
| `accentSelected` | base |
| `accentText` | WCAG luminance > 0.6 选黑字, 否则白字 |

跨 `ui_theme_set_mode(LIGHT/DARK)` 保留 — 切深浅色后还是同一品牌色.

#### 影响范围

所有走 `theme::kAccent()` / `kAccentHover()` 等的 widget 自动跟随:
- `<button type="primary">` (background)
- `<input type="range">` slider fill + thumb
- `<progress-bar>` fill
- `<input>` focus 底部下划线
- nav-item `.sel` 蓝高亮
- ContextMenu hover / 选中
- 其他主题色相关元素

#### 验证

`build/theme-test/`: 加载页面后切 default-blue → green-#218554 → orange-#eb8c12
→ alpha=0 reset → 回 default-blue. 4 张截图全对.

#### 内部

- `theme.h` 新增 `AccentOverrideActive() / AccentOverrideValue()` 状态 +
  `ApplyAccent(base)` 派生 + `SetAccent(base)` 入口
- `SetMode` 切深浅色后会重新 `ApplyAccent` (override 跨模式保留)
- `ui_api.cpp` 新 `ui_theme_set_accent(UiColor)`, 自动 `InvalidateAllWindows`

## 1.4.0 — build 27

### `ui_debug_screenshot_menu` + 修 menu icon 不渲染

build 26 ship 之后用户问"截图你看到图标了？"，一句话戳穿——之前我用
`[Graphics]::CopyFromScreen` 全屏抓图，缩到这个比例 menu icon 16x16 完
全看不清，**根本没真验证 phase B/C 的 SVG / IMG 是否在跑**。

#### 新 API：`ui_debug_screenshot_menu`

直接抓 ContextMenu popup HWND 的 D2D RT 内容（跟 `ui_debug_screenshot`
同款 readback 路径，但读 `popupRenderer_` 而不是 window renderer）。
Pipe 命令 `screenshot_menu <path>`。

```c
UI_API int ui_debug_screenshot_menu(UiWindow win, const wchar_t* outPath);
```

PNG 是 popup 自己尺寸（不带桌面 / 主窗口）。

#### 修 menu icon 不渲染

用了 `screenshot_menu` 拍下来一看 Save / Open / Cut / Copy / Paste **全
没图标**——只有 Quit 是红色的（Phase D OK）。

根因：`WireSubtreeMenus` 调 `Renderer::ParseSvgIcon` 跟
`LoadImageFromBytes` 用的是 `ui::g_activeRenderer`——这个全局只在
paint 期间（BeginDraw / EndDraw 之间）有效。`Attach` / v-if mount 走
的都不在 paint context，**g_activeRenderer 是 nullptr**，icon 创建
路径直接被 if 条件 short-circuit 跳过去了。

修：`WireSubtreeMenus` 通过 `winHandle_` 取 `WindowImpl::GetRenderer()`，
跟 paint 状态解耦。Attach / v-if mount 都能拿到工厂。`g_activeRenderer`
作为兜底（理论上不该 reach 到）。

#### 验证

`build/demo-shots/popup_file.png`：💾 Save / 📁 Open / sep / Recent ▸ /
sep / ⏻ **Quit (红色)** —— 4 个 phase 全对。

`build/demo-shots/popup_rclick.png`：✂️ Cut / 📄 Copy / 📋 Paste —— 三
个 SVG icon 都跟着文字色（黑），currentColor 路径正确。

## 1.4.0 — build 26

### Menu 加 icon (SVG / IMG) + submenu + per-item CSS color

四个 phase 一气呵成,把 ContextMenu 的功能补到现代水准:

#### Phase A — submenu

```html
<menu trigger="#fileBtn">
  <menuitem id="1">Save</menuitem>
  <menu text="Recent">                <!-- 嵌套 <menu> = submenu -->
    <menuitem id="10">file1.txt</menuitem>
    <menuitem id="11">file2.txt</menuitem>
  </menu>
</menu>
```

`<menu text="...">` 嵌在父 `<menu>` 里编译成 `CompiledMenu` 用 shared_ptr
作为父 `CompiledMenuItem.submenu` 字段。`WireSubtreeMenus` 递归构建
`ContextMenuPtr` 调 `parent->AddSubmenu(text, child)`。

#### Phase B — inline SVG icon

```html
<menuitem id="1" onclick="onSave">
  <svg viewBox="0 0 24 24"><path fill="currentColor" d="..."/></svg>
  Save
</menuitem>
```

`<menuitem>` 子节点里第一个 `<svg>` 序列化回字符串,运行时
`ParseSvgIcon` 转 D2D path,`DrawSvgIcon(icon, rect, textColor)` 让
icon 跟文字色一起变(`fill="currentColor"` 习惯)。

#### Phase C — `<img src>` 走 asset resolver

```html
<menuitem icon="logo.png">Item with PNG</menuitem>
<!-- 或子节点: -->
<menuitem><img src="logo.png"/>Item</menuitem>
```

跟 `<img>` widget 同一个 resolver chain (asset::Resolve →
`Renderer::LoadImageFromBytes` → `ID2D1Bitmap`)。dev 走
`ui_asset_register_dir`, ship 走 `ui_asset_register_blob` (CMake
`ui_core_embed_binary` 烤进 exe)。**HTML 不变**。

ContextMenu 新 API: `AddItemBitmap(id, text, shortcut, ID2D1Bitmap)`。
Draw 路径: `bitmap` 优先于 SVG; bitmap 用 `RT()->DrawBitmap` 直接
blit (不变色, 跟 SVG 不同)。

#### Phase D — per-item CSS color override

```html
<menuitem style="color: #d63a26">Quit</menuitem>
```

`<menuitem style>` 解析 `color:` 值 (没引入 CSS parser, 直接 string
扫一段),存到 `CompiledMenuItem.color_*`。ContextMenu MenuItem 加
`hasColor + overrideColor`,`Draw` 用它代替默认 textColor。SVG icon
也跟着变色 (DrawSvgIcon 接 color 参数);PNG icon 不变色。

新 API: `ContextMenu::SetLastItemColor(D2D1_COLOR_F)` (链式调用风格,
配合 `AddItem` / `AddItemEx` / `AddItemBitmap`)。

#### Demo

`demo/ui_demo.html` Menu 页扩展:
- 按钮下拉: Save (软盘 SVG) / Open (文件夹 SVG) / —— / **Recent** (嵌套
  submenu, 含 file1/2/3.txt) / —— / **Quit** (退出 SVG icon, **红色文字**)
- 区域右键: Cut / Copy / Paste 各带 SVG icon

跑 `clang-cl-build/ui-demo-html.exe` → 侧栏 Menu → 点 File 按钮 / 右键
灰色面板,所有 4 个 phase 在一个截图里展示完整。

#### 内部

- `compiled_page.h`: `CompiledMenuItem` 加 `submenu` (递归 ptr) /
  `iconSvg` / `imgSrc` / `hasColor + color_*`
- `compiler.cpp`: `<menu>` 编译 lambda 化, 支持 nested; menuitem 解析
  `icon` / `style` 属性; 子节点扫 `<svg>` / `<img>`; 加 `serializeNode`
  把 SVG 元素 → 字符串
- `page_state.cpp`: `WireSubtreeMenus` 递归 build, 支持 bitmap /
  override color
- `context_menu.h/cpp`: MenuItem 加 `bitmap / hasColor / overrideColor`;
  新 `AddItemBitmap` / `SetLastItemColor`; Draw 用 override 文字色 +
  bitmap 优先于 SVG
- `renderer.h`: 集中 `extern Renderer* g_activeRenderer`(给嵌套
  namespace 共用)

## 1.4.0 — build 25

### 修：v-if 子树里的 `<menu>` 没接上 trigger

build 24 demo 加了 Menu 页（`v-if="active == 9"`），跑起来发现点 File
按钮不弹菜单，右键面板也不弹。

根因：`WireMenus` 在 `PageState::Attach` 时跑一次，但 `<menu trigger="#menuBtn">`
的触发元素 (`#menuBtn` 在 page 9 v-if 块里) 在 Attach 时**还没 mount**，
`FindById` 返回 nullptr，触发 spec 直接跳过 → 永不挂回调。

类似的：`<menuitem onclick="onMenuSave">` 收集到 `menuItemHandlers_` 也是
v-if mount 之后才发生的，但 `AttachWindow` 的 `onMenuItemClick` wrapper
在 mount 前就装好了，**而且捕获的是 handlers 表的快照**，新加进来的
itemId→method 名映射 wrapper 看不见。

#### 修

1. **抽 `PageState::WireSubtreeMenus`**：处理单批 CompiledMenu —— 实例化
   ContextMenu、注册 itemId→handler、立即装 click trigger（如果已 AttachWindow）、
   重装 rclick wrapper（rclick wrapper 用的是 triggers_ 列表的快照，新增
   trigger 必须重装）
2. **`UpdateConditional` v-if mount 后调它**：`sub.menus` 里的 `<menu>` 也
   接上 trigger + 进派发表
3. **`onMenuItemClick` wrapper 不再捕快照**：通过 `self->menuItemHandlers_`
   实时查，运行时新加的条目立即可见

#### 验证

`build/demo-shots/dispatch_save.png` / `dispatch_paste.png`：
- 点 File 按钮 → menu 弹出 → 选 Save → label 刷成 "Save — handled by HTML methods"
- 在面板上右键 → context menu 弹出 → 选 Paste → label 刷成 "Paste — handled by HTML methods"

`menu_open_full.png` 是个有意思的截图：用 `[Graphics]::CopyFromScreen`
全屏抓的，**popup HWND 也被捕到了**——能直接看到 File ▾ 下面浮起来的
"Save / Open / —— / Quit" 三项 + 快捷键标记。

## 1.4.0 — build 24

### Demo: 新增 "Menu" 页演示 build 23 的 `<menu trigger>` 自动挂载

`demo/ui_demo.html` 加了第 9 个 nav 项 (Menu)，里面 2 个例子：

- **Click trigger**：`<button id="menuBtn">File ▾</button>` + `<menu trigger="#menuBtn" event="click">` → 点按钮弹下拉菜单（Save / Open / Quit）
- **Rclick trigger**：`<div id="menuPanel" ...>` + `<menu trigger="#menuPanel" event="rclick">` → 区域内右键弹上下文菜单（Cut / Copy / Paste）

每个 `<menuitem onclick="onMenuSave">` 直接调 `<state>.methods.onMenuSave`
更新 reactive `menuLastAction`，**C 端 demo 入口 (`demo/ui_demo_html.cpp`) 不用任何 ui_menu_* / ui_window_on_menu 代码**。

`ui-demo-html.exe` 跑起来侧栏新增 "Menu" 项可以试。

## 1.4.0 — build 23

### `<menu trigger>` 自动挂载 + `<menuitem onclick>` 自动派发

build 22 留的 TODO 全做了 —— HTML 端写一行就能玩转上下文菜单：

```html
<state>
  data: { last: "(none)" },
  methods: { onSave: last = "Saved!", onOpen: last = "Opened!" }
</state>

<div class="root">
  <!-- 按钮点击弹下拉菜单 -->
  <menu trigger="#btnFile" event="click">
    <menuitem id="1" onclick="onSave">Save</menuitem>
    <menuitem id="2" shortcut="Ctrl+O" onclick="onOpen">Open</menuitem>
    <separator/>
    <menuitem id="3">Quit</menuitem>
  </menu>

  <!-- area 内右键弹上下文菜单 -->
  <menu trigger="#area" event="rclick">
    <menuitem id="11" onclick="onSave">Save</menuitem>
  </menu>

  <button id="btnFile">File</button>
  <div id="area" style="height: 80px; ...">Right-click me</div>
  <label>{{ last }}</label>
</div>
```

C 端**不用一行代码**——`<menuitem onclick="onSave">` 直接调 `<state>.methods.onSave`
（或 `ui_page_set_handler(page, "onSave", ...)` 注册的 C 函数）。

#### 内部

`PageState` 新增 `AttachWindow(uint64_t winHandle)`，`ui_page_open_window`
拿到 HWND 后调一次：

- **click trigger** → trigger element 的 `onClick` lambda 调
  `WindowImpl::ShowMenu(menu, x, y)`（在按钮左下角弹出）
- **rclick trigger** → wrap `window->onRightClick`，命中 trigger 元素就
  show 对应菜单。chain 之前注册的 `ui_window_on_right_click` callback。
- **onclick dispatch** → wrap `window->onMenuItemClick`，按 itemId 查
  `menuItemHandlers_` 找 method 名，调 `handlers_[name]()`。chain 之前的
  `ui_window_on_menu` callback。

`WireMenus` 只收集 trigger 列表（HWND 还没拿到），实际安装 callback
推迟到 `AttachWindow` —— 必须用真 HWND，否则 ShowMenu 走 `GetActiveWindow()`
fallback 在 debug-driven 测试 / 后台窗口场景下拿不到正确 hwnd。

#### 验证

`build/menu-test/auto-test.exe`：
- 模拟 click `#btnFile` → 菜单弹出 → click_id(2) → `last = "Open (handled by HTML methods)"`
- 模拟 rclick `#area` → 菜单弹出 → click_id(11) → `last = "Save (handled by HTML methods)"`

`auto_after_click.png` / `auto_after_rclick.png` 双截图都对。

#### 取消的 build 22 警告

build 22 编译期警告 `<menuitem onclick='X'>: auto-dispatch not yet implemented`
已删除——现在真的 dispatch 了。

## 1.4.0 — build 22

### HTML `<menu>` / `<menuitem>` 声明式 — 弃 .ui 第 3 步

```html
<div class="root">
  <menu id="ctx">
    <menuitem id="1">Save</menuitem>
    <menuitem id="2" shortcut="Ctrl+O">Open</menuitem>
    <separator/>
    <menuitem id="3">Quit</menuitem>
  </menu>
  <button id="trig">Show menu</button>
</div>
```

```c
UiMenu m = ui_page_menu(page, "ctx");      /* 拿到 ContextMenu handle */
ui_menu_show(win, m, x, y);                /* 跟 ui_menu_create 出来的一样 */
ui_window_on_menu(win, on_menu_item, NULL); /* 派发点击 → switch(itemId) */
```

#### 内部

- `compiled_page.h`: 新 `CompiledMenu` / `CompiledMenuItem`
- `compiler.cpp`: 拦截 `<menu>` 元素, 解析子 `<menuitem>` / `<separator>`,
  推到 `out->menus` (不创建 widget, 不递归)
- `page_state.h/cpp`: `WireMenus()` 把 `CompiledMenu` 实例化成
  `shared_ptr<ContextMenu>` 存 `menus_` + `menuById_`. 通过
  `FindMenuById(name)` / `GetOrRegisterMenuHandle(name)` 查
- `page_api.cpp`: 新 C API `ui_page_menu(page, name) → UiMenu`
- HTML 必须单一根元素, `<menu>` 放在根 `<div>` 里就行

#### Review 反馈修复

| 级别 | 问题 | 修复 |
|---|---|---|
| HIGH H1 | `ui_page_menu` 每次调用都 `RegisterMenu` 新 handle, 多次同 name 调用导致 Context::menus_ 无限增长 | PageState 缓存 `name → handle`, 复用 |
| HIGH H2 | `ui_page_reload` 后旧 handle 指向陈旧 ContextMenu (新 menu 对象, 旧句柄不刷新) | `Attach()` 调 `InvalidateMenuHandles()` 把缓存的 handle 全部 RemoveMenu, 用户下次 `ui_page_menu` 拿到对应新 menu 的新 handle |
| MEDIUM M1 | `<menuitem>` 出现在 `<menu>` 外: 工厂 fallthrough 成空 VBox | 编译期报错 "must be a direct child of `<menu>`" |
| MEDIUM M3 | `<menuitem id="abc">` 非数字 id 被静默吞掉 | 编译期报错 "id must be integer" |
| MEDIUM M4 | `<menuitem onclick="method">` 收集了但暂未派发 (build 22 没做自动调度), 用户会以为 onclick 起作用 | 编译期警告 "auto-dispatch not yet implemented; use ui_window_on_menu" |

#### 已知未做 (build 23+)

- `<menu trigger="#elem" event="click|rclick">` 自动挂载到 trigger 元素的 click/rclick
- `<menuitem onclick="method">` 自动派发到 PageState 方法
- `<submenu>` 嵌套子菜单
- 当前用户必须 `ui_window_on_menu` + switch 派发, 跟 .ui MenuBar 同款

#### 验证

`build/menu-test/`: 加载 HTML, `ui_page_menu(p, "ctx")` 返回 handle=1,
`ui_menu_show` 弹出, `ui_debug_menu_item_count = 4`(3 items + 1 sep),
`ui_debug_menu_click_id(2)` 触发 window 回调, 反应式 `last` 刷成 "Open",
`result_after_click.png` 显示 "Last clicked: Open".

## 1.4.0 — build 21

### HTML `<custom>` 标签 — 自绘 widget

迈向 1.5.0 弃 `.ui` 第 2 步：HTML 里直接声明自绘 widget，跟 `.ui` 的
`<Custom>` 对齐。

```html
<custom id="canvas" style="width: 440px; height: 240px;"/>
```

```c
UiWidget root   = ui_page_root(page);
UiWidget canvas = ui_widget_find_by_id(root, "canvas");
ui_custom_on_draw      (canvas, my_paint,    NULL);
ui_custom_on_mouse_down(canvas, my_on_click, NULL);
```

C 回调里用现有 `ui_draw_*` 系列绘图 API（fill_rect / draw_text /
fill_rounded_rect 等），跟 `ui_custom()` 创建路径完全一样。`<Custom>`
大写别名也认（`.ui` 习惯）。

### CRITICAL 修复 — `ui_widget_find_by_id` 回填 `apiHandle`

review 发现的问题：`ui_custom()` 创建路径在工厂里就 `apiHandle = h`，
但 HTML / `.ui` markup 工厂构造时拿不到 handle，所以 `apiHandle` 一直
是 0。`CustomWidget::OnDraw` 把 `apiHandle` 当 `UiWidget` 传给
`drawCb`，用户在回调里拿这个 0 调 `ui_widget_invalidate(w)` /
`ui_custom_focus(w)` 全部 silent no-op。

修：`ui_widget_find_by_id` 第一次 Insert 后，dynamic_cast 检测
`CustomWidget` 并回填 `apiHandle = h`。从此两条路径行为一致。

### 验证

`build/custom-test/`：HTML `<custom id="canvas">` + 3 次模拟点击
（debug pipe），回调里把收到的 `w` 写日志，3 次结果都跟
`ui_widget_find_by_id` 返回的 handle 一致：

```
canvas=3 paintHandle=3 clickHandle=3
```

视觉回归：`result_clicked.png` 显示 3 个蓝色圆点 + "Hits: 3"
（reactive 状态写回 `ui_page_set_int`）。

## 1.4.0 — build 20

### HTML page i18n: `.lang` 文件 + `@key` 语法糖

迈向 1.5.0 弃 `.ui` 第 1 步：HTML page 子系统补 i18n 跟 `.ui` 对齐。

PageState 已有 `$t(key)` 响应式函数 + `$locale` reactive 变量（自动重新求值），
本次补两块：

#### `.lang` 文件加载

```c
UI_API void ui_page_load_language_string(UiPage p, const char* locale,
                                          const char* utf8_content);
UI_API int  ui_page_load_language_file  (UiPage p, const char* locale,
                                          const wchar_t* path);
```

格式跟 `.ui` markup 完全一致：UTF-8、每行 `key=value`、`#` / `;` 行注释、
BOM 自动去掉、CRLF 容忍。语义是**replace-not-merge**（同 locale 二次调用
覆盖前一次表，跟 markup `LoadLanguageString` 一致）。

#### `@key` 语法糖

`<label>@app.title</label>` 编译期 desugar 成 `<label>{{ $t('app.title') }}</label>`。
`@` 后只接 `[A-Za-z0-9_.-]`，整段（trim 后）必须是单一 i18n 引用，**不允许**
混合内容（"Hello @x" 不算，用户用 `{{ }}` 显式拼接）。

迁移友好：从 `.ui` 来的用户文本里 `text="@btn.save"` 直接对应 HTML
`<button>@btn.save</button>`，**`.lang` 文件一字不改**。

#### 试跑

`build/i18n-test/`：HTML 加载 zh.lang / en.lang，`$locale` 切换两次：
- `result_zh.png`: "i18n 测试页面 / 演示 .lang 文件 + @key 语法糖 / 点击 +1 / 计数: 0 / 当前语言: zh"
- `result_en.png`: "i18n Test Page / .lang file + @key shorthand / Click +1 / Count: 0 / Current locale: en"

`@key` shorthand + `{{ $t('key') }}` 显式调用 + `$locale` 直接读，三种用法都正常。

#### 内部

- `compiler.cpp`: 新增 `AsI18nKey()` 检测 `@key` 形式，`BuildTextExpr()`
  / `ExtractStaticText()` 改写成 `Call(Identifier "$t", StringLit key)` AST
- `page_api.cpp`: 新增 `OpenWideBinary()` 共享 helper（之前 `ui_page_load_file`
  里散落的 MSVC / MinGW 宽路径处理逻辑），`ParseLangContent()` 解析 `.lang` 字节
- BOM 检查从 per-line 提到 per-file 起点（review 反馈）

## 1.4.0 — build 19

### 资源解析器（HTML `<img>` + `<link rel="stylesheet">`）

之前 HTML 只能用内联 `<style>` 和内联 SVG —— 想加 PNG 图标或外部 CSS
没有路径。这次加一个**资源解析器**抽象，dev / ship 两种工作流共用一份
HTML：

- dev：`ui_asset_register_dir("assets/")` 边改边看
- ship：CMake `ui_core_embed_binary()` 烤进 exe → `ui_asset_register_blob()`
  注册，单文件分发

#### 新增 C API

```c
typedef int (*UiAssetResolver)(const char* name,
                                const void** out_bytes, size_t* out_size,
                                void* userdata);

UI_API void ui_asset_register_dir     (const char* dir_utf8);
UI_API void ui_asset_register_blob    (const char* name, const void* bytes, size_t size);
UI_API void ui_asset_register_resolver(UiAssetResolver fn, void* userdata);
UI_API void ui_asset_reset            (void);
```

注册顺序就是匹配优先级，先注册先匹配。

#### 新增 CMake helper

`ui_core_embed_binary(<target> FILE <p> OUT <h> VAR <name>)` —— 跟
`ui_core_embed_text` 对称，但生成 `unsigned char[]` 数组（无终结符），
PNG / JPG / 任意 blob 都能烤进头文件。

#### HTML 标签

- `<img src="logo.png" object-fit="contain">` — 走 `ImageWidget`，
  src 是 ui::asset 的 key。object-fit 支持 `fill` / `contain`(默认) /
  `cover` / `none`
- `<link rel="stylesheet" href="theme.css">` — href 走资源解析器，CSS
  内容并入页面 stylesheet。Cascade 顺序：link 在前，inline `<style>` 在后
  覆盖 link（跟浏览器一致）

#### 内部新增

- `Renderer::LoadImageFromBytes(bytes, size)` — 通过 `IWICStream::InitializeFromMemory`
  从内存解码 PNG/JPG/BMP/ICO/...；strip-decode 路径跟 LoadImageFromFile 共享
- `ImageWidget`（轻量级，跟 ImageViewWidget 区分：那个是带缩放/平移/SVG/GIF
  的专业组件，这个只为 `<img>` 服务）
- `ui::asset::Resolve(name, ...)` C++ 内部入口

#### 试跑

`build/asset-test/`：HTML 引用外部 PNG + CSS，结果截图见
`build/asset-test/result.png`。link 蓝标题被 inline style 覆盖成红色，
PNG logo 正确解码绘制 → cascade + image pipeline 双 OK。

## 1.4.0 — build 18

### release-package 现在会强制重建 core-ui-static.lib

**踩了一个坑**：build 17 里把 `LabelWidget` vAlign 改成 CENTER 之后，
`core-ui.dll` 的 FileVersion 是 1.4.0.17 没问题，但 `lib/static/core-ui.lib`
md5 跟好几次构建之前一样（**没重建**），ChromePlusGUI 静态链接这个旧 lib
之后看到的还是 build 15 的 NEAR 顶对齐效果。

根因：`add_custom_target(release-package COMMAND cmake --install ...)` 默认
**不依赖**任何 build target。`cmake --install` 本身只复制文件不触发编译，
ninja `--target release-package` 拿到陈旧的 `core-ui-static.lib` 就照原样
复制进 `release/core-ui-v1.4.0/lib/static/`。`core-ui` (DLL) 因为
`ui-demo-html` 间接依赖触发了重建，所以 dll 版本对得上；`core-ui-static`
没人间接依赖它就没重建。

修：`add_dependencies(release-package core-ui core-ui-static ui-demo
ui-demo-html)`，install 之前先把所有 release artifact 重编一遍。

下游确认：
- 之前的 `core-ui-static.lib` md5 = `7b10a9fd...`（build 15 时刻的快照）
- 现在的 `core-ui-static.lib` md5 = `769a287b...`（带 build 17 vAlign CENTER 修复）
- ChromePlusGUI 的 `lib/core-ui-v1.4.0/lib/static/core-ui.lib` 已同步更新

## 1.4.0 — build 17

### Label 文字垂直居中（修侧边菜单 icon+文字错位）

ChromePlusGUI 反馈：侧边菜单的图标和文字不是垂直居中，文字偏上。两条候选
原因都对在点上：
1. `LabelWidget::OnDraw` 在 `wrap_=true` 时用 `DWRITE_PARAGRAPH_ALIGNMENT_NEAR`
   （顶对齐）而非 CENTER
2. `LabelWidget::SizeHint` wrap 模式下返回 `h = textH + 6.0f`，多出 6px slack

NEAR 把 6px slack 全堆在 rect 底部 → 文字被钉在 rect 顶部；icon 走的是几何
中心对齐（HBox `align-items: center` + svg 自身 rect 紧贴 viewBox），结果
icon 中心比 label 文字基线低 3px 左右，肉眼能看到错位。

HTML factory 默认 `<label>/<span>/<a>/<small>/<strong>/<em>/<p>` 都
`SetWrap(true)`，所以**所有 HTML 文本节点**都吃这个亏，侧边菜单只是最显眼的。

修：`LabelWidget::OnDraw` 把 vAlign 直接改成 `CENTER`：

- 单行 wrap（侧边菜单 / 表单 lbl / 卡片标题）：6px slack 平分到上下，跟
  HBox 兄弟节点（icon / input / button）的几何中心完美对齐
- 多行 wrap（段落文本）：rect 紧贴内容块，CENTER ≈ NEAR，不会让段落
  上下塌进中心
- 不影响已有的 wrap=false 路径（之前就是 CENTER）

视觉回归：`build/textarea-test/sidebar_after.png` — 侧边菜单 icon 跟标签
（Home / Button / Selection / ...）水平基线对齐。

## 1.4.0 — build 16

### TextArea 选中文本前景色（白） + 验证 soft-wrap

build 15 把 TextArea 切到 `IDWriteTextLayout` 之后，ChromePlusGUI 又反馈：
1. 多行文本框选中区域文字颜色没有跟着选中背景反相，看不清；浏览器 / TextInput
   选中态下文字应该是白色（配 accent 蓝背景）
2. （误报）"内容也没自动换行" — 实际上 build 15 默认就是 wrap=true，
   验证 demo 截图三行 soft-wrap 正常工作

修：

- `TextAreaWidget::OnDraw` 选中分支补上 selection foreground：
  - `focused` → `selFg = css.hasSelTextFg ? css.selTextFg : theme::white`
  - `!focused` → `selFg = css.selTextFgInactive`（如果设置了）
  - 使用 `HitTestTextRange` 拿到的每行选区矩形作为 clip rect，第二次
    `DrawTextLayout` 用 selFg 重画选中字形（共享同一缓存 layout，几乎零开销）
- 跟 TextInput 的选区文字色逻辑一致，CSS 也通用 `selection-color` /
  `selection-inactive-color`

视觉回归：`build/textarea-test/wrap_selected.png` — drag-select 后，选中段
"a long sentence that should auto-wrap because the textarea ha" 显示白字蓝底，
未选中段保持原 fg 色；同时三行 soft-wrap 正常工作。

## 1.4.0 — build 15

### TextArea 切到 IDWriteTextLayout 共享 hit-test + draw 路径

ChromePlusGUI 反馈两个独立问题：
1. 多行文本框点击位置和光标渲染位置不一样，光标总是比鼠标"后一些"
2. 长文本超过宽度被 ellipsize 成 "..."，应该 soft-wrap

调查：
- Hit-test (`CharIndexFromXY`) 用 per-substring `MeasureTextWidth(substr_0..i)`
  累加列宽；draw 用整行 `IDWriteTextLayout` 的 shaping/kerning。两者对同一文本
  在同一字号下计算出的字符位置不完全一致（DWrite 整行 layout 会做 kerning
  收紧），结果 round-trip click→pos→caretX 跟实际 click 错位
- TextArea 按 `\n` 切 logical line 后，每行调 `r.DrawText(wordWrap=false)`，
  渲染层走 ellipsis trim 路径。textarea 应该走 soft-wrap（DOM `<textarea>`
  默认 `wrap="soft"`）

修：把整个 TextArea 测量/渲染重构到 `IDWriteTextLayout` 上，hit-test、caret
定位、selection 渲染、文本 draw **全部共享同一 layout**，自然消除偏差：

#### 新增

- `Renderer::CreateTextLayout(text, w, h, fontSize, wrap)` — 公开 helper，
  按需建 IDWriteTextLayout 并设 wrap 模式
- `TextAreaWidget::wrap_` 字段（默认 `true`，匹配浏览器 textarea）+
  `SetWrap(bool)` setter
- HTML factory：`<textarea wrap="off|hard|false|0">` 关 wrap，其他都开
- `EnsureLayout(r, fontSize)` — 缓存 layout，text/maxW/fontSize/wrap 任一变化
  时重建
- `HitTestPosFromXY(x, y)` — 直接调 `IDWriteTextLayout::HitTestPoint`
- `CaretXYForPos(pos, x, y, h)` — 调 `IDWriteTextLayout::HitTestTextPosition`
- `PosUp/PosDown/PosLineStart/PosLineEnd` — 用 layout 做"视觉行"导航，
  arrow keys 在 wrap 软换行的视觉行上跳，不再按 logical `\n` 行跳

#### 删除

- 旧 `GetLines()` / `GetLineCol()` / `GetPosFromLineCol()` / `CharIndexFromXY()` 全部移除（被 layout 替代）
- 旧 `lineHeight_` 成员（改为按 layout 实时取）

#### 渲染

- 文本：单次 `ID2D1RenderTarget::DrawTextLayout(origin, layout, brush)`
- Selection：`IDWriteTextLayout::HitTestTextRange` 返回精确字形矩形，跨视觉
  行也正确高亮（多行选中跟着 wrap 走）
- Caret：`HitTestTextPosition(cursorPos)` 给精确 (x, y, h)
- ContentHeight：用 `layout->GetMetrics().height`，wrap 后真实高度

## 1.4.0 — build 14

### 修复

- **`LabelWidget::SizeHint` wrap 模式宽度反馈循环**：build 9 把 `<label>` 默认
  改 wrap=true 之后，wrap 路径里 `w = parentW;` 把 label SizeHint 宽度撑到
  父容器全宽。当 label 是 flex row 里多个子项之一时（cc-block 里 svg + label，
  或同一行多 cc-block），父容器 SizeHint 含 child 的全 parent 宽 → 父再分配
  更大宽度 → child 再 SizeHint 又含新父宽 → 累积反馈成几十万 px。
  实测 cc-block 单个被吹到 27000+ px，整行右溢出覆盖滚动条。
  修：wrap 路径下 `w` 保持自然文本宽 `estW`，仅按 parent 内容宽 cap，不再
  无条件撑成 parentW。height 计算照常用 parentW 作为 availW 测换行。

## 1.4.0 — build 13

### 修复

- **`currentColor` 沿父级继承**：之前只取 SvgWidget 自己的 `css.fg`（`<svg>`
  元素直接设的 `color`），父 `<div style="color: red">` 不会继承下来。
  现在 `OnDraw` 解析 `currentColor` 时 walk 父链找最近一个 `css.hasFg` 的祖先，
  匹配浏览器 SVG 默认 `fill: currentColor` 的继承语义。
- **SvgWidget recomputeShapes 用 `CurrentStateBits()` 不是 `lastStateBits`**：
  hover 链推进按 `unordered_set` 顺序，可能 svg 比 parent 先刷，读到 parent 的
  旧 `lastStateBits` 导致 `:hover path { fill }` 命中失败。改用 `CurrentStateBits()`
  直接读 widget 的 hovered/pressed 字段，那些在 RefreshCssState 之前就被赋值。

### Demo

- `demo/ui_demo.html` 新增 Page 7 "SVG & CSS"（替换原"Image not supported" 占位）：
  - align-items: center / start / end 三态 80×80 容器内居中 SVG
  - CSS class → path fill：5 个心形不同色（蓝/绿/红/紫/橙），无 inline `fill`
  - currentColor inherit：4 个对勾各自跟随父 `.cc-blue/green/red/purple` 的
    `color` 显示蓝/绿/红/紫
  - `:hover path` 实时切色：4 个图标 hover 时 fill 从灰变蓝 + bg 浅蓝
  - `.toggle-icon.active path` 类切重算：点心形切换 active class，bg 蓝/灰、
    path #fff/灰 同步翻面
  - 反应式 tone：4 按钮分别置 `iconColor` state，driving `.react-icon.tone-X`
    类切换，path fill 跟着变

## 1.4.0 — build 12

### CSS 真正驱动 SVG path + flex `align-items`

ChromePlusGUI 反馈两个独立但相邻的问题：
1. `<div align-items: center; justify-content: center>` 居中不了 SVG 子项（SVG 停在左上角）
2. `.dash-card-icon path { fill: var(--accent) }` 写法看起来工作（蓝色），实际是 inline `<path fill="...">` 在起作用；CSS 规则一直空跑——所以 `.dash-icon-success path { fill: var(--success) }` 在没写 inline fill 的 path 上完全没效

调查得出：
- `ApplyFlexContainerStyle` 之前不解析 `align-items`，VBox/HBox `crossAlign_` 始终默认 `Stretch`，子项有 fixedW/H 时（SVG 必有）退化成 Start = 左上角
- CSS engine 完全没有 → SVG path 通道，path 的 fill 只能由 `<path fill="...">` 静态 attr 或 `:fill="expr"` 反应式绑定驱动

#### 新增（按浏览器语义对齐）

- **A. `align-items` CSS 解析**：`flex-start/start` → `Start`，`center/baseline` → `Center`，
  `flex-end/end` → `End`，`stretch` (默认) → `Stretch`。`widget_factory.cpp:ApplyFlexContainerStyle` 加 12 行。

- **B1+B2. CSS → SVG path 通道（编译期）**：每个 `<path>`/`<circle>`/`<rect>`/...
  fold 成 `SvgShape` 时，构造合成 `MatchNode`（tag/class/id 来自 path 节点，
  parent chain 接到 SvgWidget 的 MatchNode），调 `ComputeStyle` 跑整张 stylesheet，
  命中规则的 SVG 属性 (`fill`/`stroke`/`fill-opacity`/`stroke-opacity`/
  `stroke-width`/`stroke-dasharray`/`stroke-linecap`/`stroke-linejoin`/`opacity`)
  抽出来调 `ApplySvgShapeAttr`。优先级：inline `style="..."` > stylesheet rule >
  presentation attr (`fill="..."`) > 默认。

- **B3. `currentColor` 关键字**：`SvgShape::fillIsCurrentColor` /
  `strokeIsCurrentColor` 字段，draw 时取 SvgWidget 的 `css.fg`（即 CSS `color`
  解析值）。匹配浏览器 SVG 默认 `fill: currentColor` 的语义。

- **B4. 反应式 shape 重算**：`SvgWidget::recomputeShapes` 闭包，由 widget
  的 `recomputeStyle` hook 在 hover/press/focus/类切换时调用。从 widget 树
  实时重建 ancestor MatchNode 链 → 重跑 CSS → 重写每个 SvgShape 属性。
  效果：`.parent.active .icon path { fill: red }` 类切后立刻生效；
  `:hover path { ... }` 也工作。Demo 里 `.nav-item.sel .nav-icon path { fill: #1f6feb }`
  之前空跑，现在选中行的图标真变蓝。

#### 限制（写进 doc）

- SVG `<g>` 分组、`<filter>`、`<mask>`、`<clipPath>`、SMIL animate 不支持
- path 上的 `:class` / `v-if` / `v-for` 不支持（path 不是 widget）
- `align-items: baseline` 退化为 `center`（库无基线对齐）

## 1.4.0 — build 11

### 嵌套 inline 元素：编译期清晰报错（之前是无声叠加）

ChromePlusGUI 反馈：`<label class="title">主文本 <label class="meta">v1.0</label></label>`
渲染叠在一起。原因：core-ui 没有 inline 文本流——每个 `<label>`/`<span>` 编译
成自己的 widget rect，嵌进父 inline 元素就视觉重叠。之前编译器静默接受、
Demo 显示破图、用户只能靠肉眼定位。

#### 修复

- `page/compiler.cpp` 编译 `<label>` / `<span>` / `<small>` / `<strong>` /
  `<em>` / `<a>` / `<p>` 时，发现有 element 子节点立刻往 `compiled.errors`
  推一条带行号 + col + 改法的错误，并跳过子节点编译（避免视觉叠加）：
  ```
  HTML: nested inline element <label> inside <label> at line 42, col 28.
        core-ui has no inline text flow — each text tag (label/span/.../p)
        compiles to its own widget rect, so nesting causes visual overlap.
        Skipping nested element. Fix: split into siblings inside a flex row:
          <div style="flex-direction:row; align-items:baseline; gap:6px">
            <label ...>main text</label>
            <label ...>nested text</label>
          </div>
  ```

- `page/page_api.cpp` 成功路径（compile.root 非空）也保留 `compiled.errors`
  到 `e.lastError`，并通过 `stderr` + `OutputDebugStringA` 双通道输出。
  之前只在 fatal compile 失败时才填 lastError，warning 全被吞掉。
  load 返回值仍是 success，符合"成功 + warning"标准约定。

- `docs/html-page-ai-guide.md` 「12 已知限制」表 + 「13 反模式」段更新——
  workaround 之前给的是垂直 stack（`<p>...</p><p>...</p>`），实际想要的是
  flex-row 横排，已纠正示例代码 + 加 design note 解释为什么不做 inline flow。

## 1.4.0 — build 10

### CSS `flex-wrap: wrap` 支持

`HBoxWidget` 之前完全不实现 wrap，children 总在单行排列；超过容器宽度就溢出
（不可见或被父 ScrollView 横向 clip）。HTML demo Layout 页 `min-width: 48%`
4 个按钮想要 2×2 排列，实际只有 Cell 1/2 显示，Cell 3/4 溢出右边。

### 新增

- `HBoxWidget::flexWrap_` 字段 + `FlexWrap(bool)` setter
- HTML/CSS：`flex-wrap: wrap` / `flex-wrap: wrap-reverse` 解析后驱动 `flexWrap_`
- `HBoxWidget::DoLayoutWrap()` —— 贪心装箱算法，按 children 自然宽度（含
  `min-width` percentage 解析后的尺寸）打包到多行，每行高度 = 该行最高 child，
  行之间用 `gap` 间距。每行内沿用同一套 `justify-content` / `crossAlign`。
- `HBoxWidget::SizeHint()` wrap 路径基于上一帧 `rect.width` 估算行数，返回
  多行总高度，避免父 VBox 按单行高度分配后下方 child 重叠到第二行。

### 限制

- wrap 模式下 `expanding` flex 子项保持自然宽度（不在行内 grow），匹配
  常见"min-width % 做 tile grid"用例
- `align-content` (行间分布) 暂未实现，行始终从顶部排列
- `wrap-reverse` 解析为 wrap，不真正反向

## 1.4.0 — build 9

### HTML page 子系统多项 bug 修复

通过 IPC pipe 端到端 review HTML demo 找到的 6 个 bug，全部修复。

#### 控件属性 / 数据绑定

- **`<input type="range">` / `<input type="number">` 现在解析 `min` / `max` /
  `value` / `step` 静态属性**。之前硬编码 `SliderWidget(0,100,50)` /
  `NumberBoxWidget(0,100,0,1)`，导致 `<input type="range" min=10 max=200 value=100>`
  thumb 跑到最右、`<input type="number" step=0.1>` 显示截断成整数。
- **NumberBox 自动从 `step` 推导显示 decimals**（step=0.1 → 1 位小数，
  step=0.01 → 2 位）。之前 `decimals_=0` 默认值会把 0.5 显示成 "0"。
- **新增 `:selected` / `:checked` 反应式绑定**给 radio / checkbox / toggle，
  让 `<input type="radio" :selected="size=='medium'">` 模式可用。之前只有
  `:class="..."` 间接路径，不会驱动 widget 内部 selected_ 状态。
- **Slider v-model 写回保留 int 类型**：state 初始 `vol: 65` (int) 被拖动后
  不再变成 `30.313...`。SliderWidget 没有 step 概念，在 v-model 写回这层
  判断"原值是不是整数"，是就 round。

#### 布局

- **CSS `min-width` / `max-width` / `min-height` / `max-height` 现在支持
  百分比单位**（如 `min-width: 48%`）。新增 `Widget::percentMinW/H` /
  `percentMaxW/H` 字段，layout 时按 parent 内容尺寸解析。之前 ResolvePx
  对百分比传 parentSize=0 → 直接返回 0，所有 `min-width:48%` 退化为 0。

#### 渲染

- **HTML `<label>` 默认 wrap=true**，匹配 DOM `<label>` / `<span>` 的预期。
  之前默认 single-line + ellipsis，长文本会被截成 "..."。
- **`<ProgressBar indeterminate="true">` 解析新属性 `indeterminate`**。
  动画驱动（UpdateToggleAnimTimer 已经检测了 IsIndeterminate）+ 渲染
  （OnDraw 已实现 phase 移动条）本来就在，缺的只是 attr 解析。

#### 单选互斥

- **`RadioButtonWidget::DeselectSiblings()` 现在遍历整棵 widget 树**，不再
  只看直接 parent 的 children。HTML 里每个 radio 通常各自包在
  `<div class="row">` 里，DOM 上不是直接 siblings——按 group `name` 全局
  互斥才对。

#### Demo HTML

- `demo/ui_demo.html` Selection 页 radio 改用新 `:selected` 模式：
  ```html
  <input type="radio" name="size" :selected="size=='medium'" @click="selMedium"/>
  ```

## 1.4.0 — build 8

### 调试通道：库级 IPC server + 单 widget 截图

之前 `\\.\pipe\ui_core_debug` 协议实现位于 `demo/app.cpp` 的 `dispatchCommand`
（500+ 行），仅 `.ui` demo 自带；HTML demo / ChromePlusGUI 想要这套调试通道
要复制粘贴整段。本次抽进库内。

#### 新增公共 API（`include/ui_core.h`）

```c
/* 启动 named-pipe debug server，pipe_name=NULL 用默认 "ui_core_debug"。 */
UI_API int   ui_debug_server_start(UiWindow win, const char* pipe_name);
UI_API void  ui_debug_server_stop(void);

/* 注册自定义命令处理器；返回 >0 = 写入 out_buf 的字节数；0 = 不处理（回退
 * 到 builtin）；<0 = 错误（也回退）。可用于覆盖 builtin 或加私有命令。 */
typedef int (*UiDebugCommandHandler)(const char* cmd, const char* args,
                                     char* out_buf, int out_cap, void* userdata);
UI_API void  ui_debug_server_set_handler(UiDebugCommandHandler cb, void* userdata);

/* 单 widget 截图（widget 必须已布局）。 */
UI_API int   ui_debug_screenshot_widget(UiWindow win, UiWidget w, const wchar_t* outPath);
```

新文件 `src/ui/ui_debug_server.cpp/h`，~600 行，带完整 builtin dispatcher
（所有 50+ 通用命令，详见 `docs/debug-simulation.md`），rector demo/app.cpp
从 828 行降到 ~180 行（只保留 nav / flyout / scroll-active-page 三个 demo
特定命令，注册到 `ui_debug_server_set_handler`）。

派发顺序：用户 handler 优先（允许覆盖 builtin）→ builtin → 未识别错误。
所有命令都通过 `ui_window_invoke_sync` marshal 回 UI 线程，线程安全。

#### `ui_window.cpp` Screenshot 重构

`Screenshot(path)` 拆出 `ScreenshotRegion(D2D1_RECT_F, path)`，老接口走全
窗口（空 region）。区域以 DIP 为单位，内部按 `dpiScale_` 转像素并 clamp 到
window 边界。`ui_debug_screenshot_widget` 直接拿 widget.rect 调用即可。

#### HTML demo 一行接入

`demo/ui_demo_html.cpp` 加一行 `ui_debug_server_start(win, NULL)` 就拥有完
整调试通道。`scripts/debug-smoke-html.ps1` 端到端跑通：tree dump、hover、
click、screenshot、screenshot_widget、wheel、dialog/menu 状态查询。

## 1.4.0 — build 7

### Dialog 改为窗口级 overlay

之前 `DialogWidget` 设计是 overlay 但实现没贯通：没重写 `OnDrawOverlay`，
窗口 `OnPaint` 也不主动画 `activeDialog_`，唯一能看到 dialog 的方式是 `add_child`
到树里——一塞就被 flex 当普通子项分空间，dialog 自己的 `OnDraw` 用分到的 `rect`
画 mask + 居中 panel，视觉上"半个页面盖了一层灰，中间一个 320×170 框"。鼠标
事件也没拦截，dialog 旁边的按钮还能点，根本不 modal。

### 修复

- **`DialogWidget` 完全脱离 widget 树**：`UiWindowImpl::OnPaint` 在
  `root_->DrawOverlays` 之后，把 `activeDialog_->rect` 强制设成全客户区，
  调 `OnDraw`。Dialog 不再需要也不应该 `add_child`。
- **Modal 鼠标拦截**：`OnMouseMove` / `OnMouseDown` / `OnMouseUp` /
  `OnMouseWheel` 入口处加 active dialog 优先派发分支，事件不再漏到下层。
- **HTML / .ui 通用**：dialog 不再依赖任何 markup tag，C 侧 `ui_dialog()` +
  `ui_dialog_show(dialog, win, ...)` 创建并显示，HTML 与 .ui 页面同样可用。

### 新增：dialog 主题模式 API

- `ui_dialog_set_theme_mode(dialog, mode)` —— `0=auto`（跟随 `theme::Current()`，
  默认）/ `1=light` / `2=dark`。等价 C++ API：
  `DialogWidget::SetThemeMode(ThemeMode::{Auto,Light,Dark})`。
- 颜色全部从 `theme::Colors` 读取，OK 按钮文字色改用 `foregroundOnBrand`
  （Fluent 规范在 light/dark 下都为近白），不再硬编码 `{1,1,1,1}`。

## 1.4.0 — build 6

### 修复

- **SVG path 紧凑小数**：`MakePathFromD` 的 `readNum` 不再贪婪吞掉多个小数点。
  Material/Fluent 风格图标常用相邻小数共享小数点的紧凑写法（如 `c0 1.1.9 2 2 2`
  实际是 `c 0 1.1 .9 2 2 2`），旧实现把 `1.1.9` 当成单 token 交给 `std::stof`，
  只能解析出 `1.1`，剩余坐标全部错位并最终触发 `break`，导致这类图标只画出第一段
  subpath 或完全消失（路径设置 / 命令行 / 快捷键 / 插件管理 / 数据清理 等）。

### 工具链

- **移除 MinGW 支持**：仅保留 MSVC（`cl.exe`）/ clang-cl 两条路。CMakeLists 在
  非 MSVC 工具链下直接 `FATAL_ERROR`，不再带 `-static-libstdc++` /
  `libwinpthread-1.dll` 等 MinGW-only 处理；release 包不再附带
  `libwinpthread-1.dll`。理由：MinGW 在 sandbox/受限环境里 cc1plus 启动经常
  silent 挂掉，clang-cl + vcvars64 一次过；维护两条路径只是徒增 install/POST_BUILD
  分支。构建走 `scripts/build-clang-cl.ps1` 或 VS Developer Prompt。

## 1.4.0 — build 5

### 新增：HTML / Vue-like 声明式页面子系统

完整的 HTML 页面引擎，支持 CSS 选择器、Flex/Grid 布局、`v-if` / `v-for` / `v-model`
等响应式指令、属性绑定 (`:attr`)、事件绑定 (`@event`)、`{{ expr }}` 文本插值，
以及 `<svg>` 内联图标 (M/L/H/V/Z/C/c/S/s/Q/q/T/t/A/a 全套 path 命令)。

### 新增：公共 C API

- `UiLayout` —— 加载 `.ui` markup 的公开 C 包装器
- `ui_core_embed_text()` CMake 帮助函数 —— 把任意文本资源烤成 inline constexpr
  头文件，让 exe 完全自包含

### 新增：动画 / 文本能力

- `:width` / `:height` 反应式绑定 + CSS `transition` 触发宽高动画
- `inline style="..."` 属性、`<button type="primary">`、`<expander>` header
- CSS 选区颜色 + inactive 失焦色；Label 文本可选中
- TitleBar 默认使用嵌入 EXE 图标 + 自定义覆盖 API

### 构建 / 工具链

- 支持 clang-cl + MSVC 工具链（与 MinGW 并存）
- release 包结构修复：libwinpthread-1.dll、core-ui.dll、README、CHANGELOG、
  全 docs/ 目录、demo/app.ui、demo/lang/*.lang、HTML sample 文件统一打入
- `lib/static/` 输出真正的静态 archive

### 修复

- `v-model` 在 `v-if` / `v-for` 内部不生效
- `<input>` / `<textarea>` 的 `placeholder` 属性未解析
- 空文本聚焦时 textarea 不绘制 caret
- HTML 页默认窗口可调整尺寸（`resizable=true`）
- `@click` 在 stateful widget 翻面后才 fire
- ImageView loading spinner 接入动画 timer 自驱动
- Button 按文本自适应宽度
- 多个 demo 收缩态 sidebar 居中、icon-btn padding、nav-label 同步动画修复

---

## 1.3.0 — build 4

### 新增：字体 / 文字渲染 C API

三层控制粒度：全局默认、单窗口覆盖、渲染模式预设。纯 C API，零 DirectWrite 样板代码。
支持中英字体分离（Latin / CJK 各走各的家族），通过 `IDWriteFontFallbackBuilder`
在 11 个 CJK Unicode 区段上做映射。

**全局默认（进程级）**
- `ui_theme_set_default_font(family)` / `ui_theme_get_default_font()`
- `ui_theme_set_cjk_font(latin, cjk)` / `ui_theme_get_cjk_latin_font()` / `ui_theme_get_cjk_cjk_font()`
- `ui_theme_set_text_render_mode(mode)` / `ui_theme_get_text_render_mode()`

**单窗口覆盖**
- `ui_window_set_default_font(win, family)`
- `ui_window_set_cjk_font(win, latin, cjk)`
- `ui_window_set_text_render_mode(win, mode)`
- `ui_window_clear_font_override(win)` —— 一次清除该窗口所有覆盖

**渲染模式枚举 `UiTextRenderMode`** ——  5 个预设覆盖 95% 场景，不需要手写 DWrite
gamma / contrast / clearTypeLevel：

| 预设                          | 风格                  | DWrite 映射                    |
|-------------------------------|-----------------------|--------------------------------|
| `UI_TEXT_RENDER_SMOOTH`       | 默认 / WinUI 风        | GRAYSCALE + NATURAL_SYMMETRIC |
| `UI_TEXT_RENDER_CLEARTYPE`    | Office / Chrome 风     | CLEARTYPE + NATURAL           |
| `UI_TEXT_RENDER_SHARP`        | 记事本最锐             | CLEARTYPE + GDI_CLASSIC       |
| `UI_TEXT_RENDER_GRAY_SHARP`   | 灰度 + 像素对齐        | GRAYSCALE + GDI_CLASSIC       |
| `UI_TEXT_RENDER_ALIASED`      | 无抗锯齿 / 像素块      | ALIASED + ALIASED             |

### 修复

- **嵌套 ContextMenu**：子菜单叶子项被点击后不再残留 root popup；父菜单内移动/点击不再
  让子菜单闪烁 关-开-关。

### Demo

- `demo/font_api_demo.cpp` —— 公开 API 版字体 / 渲染模式演示
- `demo/font_demo.cpp` —— 7 模式 × 3 字体的低层 A/B 对比，开发调参用

---

### New: Font / text-rendering C API

Three layers of control: process-wide defaults, per-window overrides, and render-mode
presets. Pure C API, zero DirectWrite boilerplate. Supports Latin / CJK font split
(each script uses its own family) via `IDWriteFontFallbackBuilder` mapping across
11 CJK Unicode blocks.

**Global defaults (process-wide)**
- `ui_theme_set_default_font(family)` / `ui_theme_get_default_font()`
- `ui_theme_set_cjk_font(latin, cjk)` / `ui_theme_get_cjk_latin_font()` / `ui_theme_get_cjk_cjk_font()`
- `ui_theme_set_text_render_mode(mode)` / `ui_theme_get_text_render_mode()`

**Per-window overrides**
- `ui_window_set_default_font(win, family)`
- `ui_window_set_cjk_font(win, latin, cjk)`
- `ui_window_set_text_render_mode(win, mode)`
- `ui_window_clear_font_override(win)` — reset all overrides on this window at once

**Render-mode enum `UiTextRenderMode`** — five presets cover 95% of cases; no need
to hand-tune DWrite gamma / contrast / clearTypeLevel:

| Preset                        | Style                         | DWrite mapping                 |
|-------------------------------|-------------------------------|--------------------------------|
| `UI_TEXT_RENDER_SMOOTH`       | default, WinUI-like           | GRAYSCALE + NATURAL_SYMMETRIC  |
| `UI_TEXT_RENDER_CLEARTYPE`    | Office / Chrome-like          | CLEARTYPE + NATURAL            |
| `UI_TEXT_RENDER_SHARP`        | Notepad-sharp                 | CLEARTYPE + GDI_CLASSIC        |
| `UI_TEXT_RENDER_GRAY_SHARP`   | grayscale, pixel-aligned      | GRAYSCALE + GDI_CLASSIC        |
| `UI_TEXT_RENDER_ALIASED`      | aliased / pixel block         | ALIASED + ALIASED              |

### Fixes

- **Nested ContextMenu**: a leaf-item click no longer leaves the root popup on screen;
  movement/clicks within a parent menu no longer cause the submenu to flicker
  closed-open-closed.

### Demos

- `demo/font_api_demo.cpp` — public-API showcase for the new font / render-mode knobs
- `demo/font_demo.cpp` — low-level 7-modes × 3-fonts A/B matrix, internal tuning tool

---

## 1.2.0 — build 3

### 新增：无边框画布模式（Frameless Canvas Mode）

面向"整个窗口就是一张画布 + 滚轮缩放 + 按住拖动窗口"类应用（如图片查看器 /
色卡面板 / 桌面挂件），补齐原先需要绕过 ui-core 直接调 Win32 的盲区。

#### 画布三件套
- `ui_window_set_min_size(win, w, h)` —— 覆盖主题默认最小尺寸（480×360），
  画布模式下窗口可以缩到图片实际大小以下
- `ui_window_set_background_mode(win, mode)` —— `mode=1` 时 OnPaint 用透明 Clear
  代替主题色填充，SetWindowPos 扩大窗口时不再闪主题色
- `ui_widget_set_drag_window(w, enable)` —— 命中该 widget 时 WM_NCHITTEST 返回
  HTCAPTION，由系统接管窗口拖动。HitTest 的深度优先机制让内部交互控件天然
  不受影响（只有命中 Panel 本身的空白处才触发拖窗）

#### 一键切换
- `ui_window_enable_canvas_mode(win, 1)` —— 一次打开 min_size=32×32 +
  bg_mode=1 + 根 dragWindow=true + 隐藏 TitleBar（如有）；=0 恢复默认

#### 窗口几何（DIP-native，原先缺失）
- `ui_window_set_rect(win, x, y, w, h)` —— 原子 SetWindowPos + 同步重绘
- `ui_window_set_size(win, w, h)` —— 保持位置改尺寸
- `ui_window_set_position(win, x, y)` —— 保持尺寸改位置
- `ui_window_get_rect_screen(win, &x, &y, &w, &h)` —— 读当前几何
- `ui_window_resize_with_anchor(win, w, h, acx, acy, sx, sy)` —— 滚轮缩放
  "光标不动"原语：resize 到 (w, h) 的同时把客户区 (acx, acy) 对齐到屏幕 (sx, sy)

#### Widget 新字段
- `Widget.dragWindow`（markup 属性 `dragWindow` / `drag-window`）

---

## 1.1.0 — build 2

### 新增：调试与事件模拟 API (`ui_debug_*`)

为自动化测试 / AI 代理操作 UI / 脚本化回归而设计的一整套事件注入接口。详见
[`docs/debug-simulation.md`](./docs/debug-simulation.md)。

- **鼠标 / 键盘模拟（内部通路）**：`ui_debug_click` / `right_click` / `hover` /
  `drag` / `wheel` / `focus` / `blur` / `key` / `type_text` 等，走真实 widget 事件
  路径，触发 onClick / onValueChanged 等回调。
- **高层控件操作**：`ui_debug_checkbox_set` / `toggle_set` / `radio_select` /
  `combo_select` / `slider_set` / `number_set` / `tab_set` / `expander_set` /
  `splitview_set` / `flyout_show` / `text_set` / `scroll_set` 等。
- **Context Menu 支持（含子菜单）**：`ui_debug_menu_is_open` / `menu_item_count_at` /
  `menu_click_path`（按整数路径 `[i0,i1,...]` 点击任意深度的菜单项）。
- **HWND 通路**：`ui_debug_post_click` / `post_right_click` / `post_key` /
  `post_char` / `pump`，通过 Win32 `PostMessage` 走真实消息循环。
- **线程安全工具**：`ui_window_invoke_sync(win, fn, ud)` 把跨线程调用 marshal 到 UI
  线程，避免工作线程直接 mutate widget 造成数据竞争。

### 新增：Widget 事件派发重构

- 抽出 `UiWindowImpl::DispatchKeyDown(vk)`，WM_KEYDOWN 和 `SimKeyDown` 共用。
  先前 Sim 版本漏掉 Tab 焦点 / Enter-Space 激活 / 方向键 Slider 等逻辑，现已补齐。
- `ContextMenu::SimulateClickPath([i0, i1, ...])` 递归点击任意深度子菜单，并把
  item id 回传给 root 菜单的父窗口。
- 新增 `ContextMenu::g_debugSuppressAutoClose` / `ui_debug_set_menu_autoclose(0)`：
  自动化脚本持有前台窗口时关掉菜单的 50ms 前台轮询自动关闭行为。

### 修复

- **`ui_widget_find_by_id`**：markup 构建的子 widget 从未登记到 handle table，
  旧实现会返回 `UI_INVALID`，实际上应当把找到的 widget 插入 handle table 再返回。
- `ui_debug_dump_widget` 现在能识别 `NavItem` / `Flyout` / `Expander` / `SplitView` /
  `NumberBox` / `TextInput` / `TextArea` 的类型名和状态字段。

### 新增：demo + pipe 调试协议

- `demo/app.cpp` 的 `\\.\pipe\ui_core_debug` 从 6 条命令扩展到 45+ 条，覆盖所有
  可交互控件。pipe 命令处理器现在通过 `ui_window_invoke_sync` 在 UI 线程上执行。
- demo 注册了一个带 "Paste Special" 子菜单的右键菜单以便测试。
- 新增 `scripts/debug-smoke.ps1` 回归脚本：60 步端到端覆盖 9 个 demo 页面，
  每步截图，生成 `smoke-report.json`。

### 文档

- 新增 [`docs/debug-simulation.md`](./docs/debug-simulation.md) —— 控件 × 操作矩阵、
  C API 参考、pipe 命令表、PowerShell / Python / C++ 示例。
- website 新增 Debug 页（`/docs/debug`）。

---

## 1.0.0 — build 1

- 首个带版本号记录的版本
- `core-ui.dll` / `ui-demo.exe` 嵌入 Windows VERSIONINFO 资源
- 新增 `ui_core_version()` / `ui_core_version_build()` / `ui_core_version_string()` 运行时 API
- 头文件暴露 `UI_CORE_VERSION_MAJOR/MINOR/PATCH/BUILD` 宏与 `UI_CORE_VERSION_STRING`
