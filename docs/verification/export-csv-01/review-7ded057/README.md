# Independent export review evidence

Exact candidate7ded057bdc706c0040d8ed7b321ee53985753d08. See ../../EXPORT-CSV-01-CODEX-REVIEW.md for findings. Raw logs and status are unchanged. Candidate selection_export test passed; reviewer probes deliberately assert the frozen requirements and failed7 assertions on Windows,8 on Linux. The first Windows ad-hoc compile lacked /MD and failed to link; the matching-runtime retry ran. No full gate was run on this rejected candidate. Generated fixtures only; no original media.

review-commands.ps1 and review-probes.cpp reproduce the targeted checks. The Linux command reuses the existing local build-stage image, disables networking and mounts candidate source read-only. It is not a docker build or full gate.
