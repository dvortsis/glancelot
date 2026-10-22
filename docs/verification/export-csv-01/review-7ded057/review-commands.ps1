# Commands used for the isolated review (targeted only, not a full gate).
# build-export-review.ps1 is the candidate build script with only the build target narrowed.
& ./.cache/build-export-review.ps1 -CoreOnly -BuildDir build-codex-review-export-csv-01
ctest --test-dir build-codex-review-export-csv-01 -R '^selection_export$' --output-on-failure
# Initial ad-hoc probe compile omitted /MD and failed to link; probe-build.txt retains that failure.
cl.exe /nologo /MD /EHsc /std:c++20 /utf-8 /I include .cache/review-probes.cpp build-codex-review-export-csv-01/glancelot_core.lib /Fe:.cache/review-probes.exe /Fo:.cache/review-probes.obj
& ./.cache/review-probes.exe
$reviewRoot=$PWD.Path
$linuxOut=Join-Path $reviewRoot '.cache/linux'
docker run --rm --network none --mount "type=bind,source=$reviewRoot,target=/review,readonly" --mount "type=bind,source=$linuxOut,target=/out" --workdir /review glancelot-watcher-harness-evidence:7e66052 sh -c 'g++ -std=c++20 -O2 -pthread -I include src/core/selection_export.cpp src/core/file_operations.cpp .cache/review-probes.cpp -o /out/review-probes && /out/review-probes'
