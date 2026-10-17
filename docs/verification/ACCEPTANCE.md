# Independent acceptance verification

The acceptance harness uses generated media inside disposable directories. It never opens or modifies personal photos. Assertions are derived from requirement outcomes rather than decoder internals.

## Baseline automated scenarios

- FMT-01 / SYS-02: truncated JPEG reports terminal error, then a valid image opens in the same controller.
- COLOR-02 / FMT-01: tagged sRGB PNG preserves known dimensions, alpha, and pixel color.
- FMT-01 / VIEW-01: JPEG EXIF orientation 6 changes dimensions and places known red/blue regions correctly.
- PERF-04 / SYS-02: rapid large-to-small selection retains the last image after superseded decode event delivery.
- IN-01 / IN-06: command IDs are discoverable, duplicate bindings are rejected, and remapping plus basic settings persist across controller lifetimes.
- NAV-03 / NAV-06: natural numeric file order, stop and current-folder loop work in both directions.

The lead must link `tests/acceptance_tests.cpp` with ViewerController, navigation, Qt Core/Gui/Concurrent/Test, enable AUTOMOC, and place `glancelot-decoder` beside the test executable. Execute with an offscreen Qt platform for automation. Configuration is redirected to a temporary directory before constructing the application.

## Additional independent review gates

Navigation must demonstrate preorder directory traversal, backward equivalence, empty-directory bypass, stable sorting, symlink-cycle avoidance, and bounded traversal root. Tests belong to the navigation assignment; reviewer reads their expectations against NAV-01 through NAV-08 and adds counterexamples if gaps exist.

Decoder review must establish a finite worker lifetime, bounded input/output messages, decoded allocation limits, generation validation before state publication, failure recovery, and no implicit source-file writes. A QImage that merely decodes is insufficient evidence for monitor-profile rendering or physical-pixel output.

## Evidence and limitations

Tests are initially prepared against the frozen API. Execution results will be recorded after controller/worker implementation is available. No result here qualifies physical Windows/Linux displays, color calibration, mixed DPI, GPU loss, high refresh, monitor hotplug, clean-machine installation, native Wayland/X11, HDR, or a performance percentile. Those require explicit separate evidence.
