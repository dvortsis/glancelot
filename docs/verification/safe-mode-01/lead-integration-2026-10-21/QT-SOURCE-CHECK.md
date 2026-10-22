# Qt6.8.3 source check for the native zero-frame failure

Checked the official Qt repository at pinned tag v6.8.3,2026-10-21. This is public implementation inspection, not a reproduction or proof of which branch executed historically.

[QQuickWindow::grabWindow](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/items/qquickwindow.cpp#L2443) can route a non-renderable window with no initialized scene graph to a synchronous offscreen grab. This confirms why a generated screenshot alone cannot prove that the visible window swapped a frame. The parent oracle correctly retains separate frameSwapped requirements.

[QSGRhiSupport::grabOffscreen](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/qsgrhisupport.cpp#L1272) creates temporary rendering infrastructure, renders/readbacks a texture, invalidates the scene-graph context, resets the target and clears the RHI pointer. This supports a mechanism for the failed run's one render callback, one invalidation and later observed=false despite RGB output. It does not explain why normal exposure/swaps were absent before the screenshot. The inspected body does not explicitly resize the window; the two-times geometry's origin is not established.

[Threaded render-loop exposure handling](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/qsgthreadedrenderloop.cpp#L1133) distinguishes exposure from obscurity and marks an unexposed swapchain non-renderable. Its existence is consistent with the zero-swap state, not proof of the external or product event that led there.

The earlier retained zero-frame report under ../process-windows/zero-frame-failure/viewer-024.json also has canvas2360x1560, DPR1, active=true, decoded image and frames0; it predates the exposure/timing fields. The passing796ab411 Windows reports have1180x780, exposed=true,23swaps. Both failures' screenshots were taken before their report fields, so no pre-grab geometry/event timeline exists for either. Neither run is relabelled a pass or a proven environmental flake.
