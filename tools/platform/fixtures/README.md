# Generated JPEG smoke fixture

smoke.jpg is a deterministic 64x48 generated JPEG, not a user photograph. It was
encoded with pinned Qt 6.8.3 by the versioned glancelot-perf-fixtures-v1 smoke
generator (image ID 12mp in smoke mode). The smoke profile deliberately uses 64x48;
this file is not a 12 MP performance fixture.

SHA256: 801532c946d6e60c7490c96ec8adb258506e854e98c14bf48966322dd597cf3b

The standalone package smoke copies it into its new disposable output directory,
checks the fixed digest, verifies actual decoder output dimensions and a presented
image frame, then rechecks the source hash. The separate generated PNG quadrant
fixture additionally checks actual screenshot source colors. No source image is
modified and neither fixture qualifies large-image performance or physical color.
