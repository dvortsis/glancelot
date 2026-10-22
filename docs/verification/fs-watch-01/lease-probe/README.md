# Linux retired-startup regression

This small QtCore-only executable compiles the actual private lease helper and
IoScheduler source. It does not replace the required FS backend regression.
It uses a generated nonexistent decoder path and no media, shell or network.

From the FS worktree, build the cached Qt stage with
`docker build --target qt -t glancelot-fs-lease-qt .`. Copy this directory to a
temporary build context, then copy `src/app/io_job_lease.hpp` and
`src/app/io_scheduler.cpp` into that context, plus
`include/glancelot/io_scheduler.hpp` under its `include/glancelot/` directory.
Run `docker build -t glancelot-fs-lease-probe <context>` and
`docker run --rm -e LANG=C.UTF-8 glancelot-fs-lease-probe`.

Before c48b77c, the Linux Qt6.8.3 probe observed Starting at retirement, then
failed: process retained, lease retained, callbacks0, jobs1, bytes3072. After
the fix, the same probe returned zero: process deleted, lease expired,
callbacks1, jobs0, bytes0. Exact output and original-source hashes are in
linux-retired-worker-{red,green}.txt and the red-source-sha256 evidence file.
Windows observed NotRunning for missing-program startup and therefore does not
qualify this asynchronous edge; its running/closed-owner cases still pass.
