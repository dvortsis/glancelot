# EN-09 handoff: feasibility documents and dependency proposals

Track session S4 ("feasibility"), Phase 0 cycle 1. Written 2026-10-23.

- **Branch:** `claude/en-09-feasibility`
- **Base:** `origin/main` at `afeac1a5d5b1da088d7b35447a933a2f64400f49` (`chore(lead): freeze Phase 0 cycle 1 contracts`)
- **Tip:** the commit that adds this handoff (parent `b1b83c3`); the exact tip SHA is in the session's final report and `git log -1 origin/claude/en-09-feasibility`.
- **Scope kept:** documents only. No source, CMake, queue, workflow, Dockerfile or `docker/` change.

## Files

| File | Content |
| --- | --- |
| `docs/F024-FEASIBILITY.md` | Visual similarity / near-duplicate groups |
| `docs/F039-FEASIBILITY.md` | Archive, comic and continuous-strip browsing |
| `docs/F048-FEASIBILITY.md` | HDR, high-bit-depth, gain-map and channel inspection |
| `docs/F071-FEASIBILITY.md` | Conversion, resize and export presets |
| `docs/F074-FEASIBILITY.md` | Remote desktop resize-to-browser |
| `docs/F076-FEASIBILITY.md` | Remote clipboard bridge and shortcut forwarding |
| `docs/F077-FEASIBILITY.md` | Browser upload/download staging area |
| `docs/F078-FEASIBILITY.md` | View-only / single-controller access and shared-session state |
| `docs/DEP-DECISIONS.md` | DEP-01..DEP-16 dependency proposals for Tracks C and D and the feasibility items |
| `docs/handoff/EN-09-HANDOFF.md` | This handoff |

## Recommendations (proposals; the lead records go/no-go)

| Item | Headline | GO slice (size, track) | Deferred / refused | Main condition that would change it |
| --- | --- | --- | --- | --- |
| F024 near-duplicates | **DEFER** | Optional early slice: hash core + worker `"hash"` mode, no UI | Whole feature until F023 (B7) and F028 (C5) integrate; embedding models no-go (download/size) | F023 and F028 integrated, then GO (XL), no new dependency |
| F039 archives / strip | **GO (two slices), DEFER rest** | A: continuous strip + spreads over folders, Qt only (L, Track A or new Track G). B: read-only ZIP/CBZ behind a new SOURCE-PROVIDER contract, libarchive in the decoder worker (XL) | RAR/CBR, 7z, tar, encrypted, solid, nested; UnRAR source permanently no-go | Lead freezes SOURCE-PROVIDER and accepts DEP-11; RAR/7z only after qualification |
| F048 HDR / precision | **GO (SDR slices), DEFER HDR output** | A: high-precision working buffer, tone-mapped SDR, truthful labels; 16/32-bit TIFF needs no new dependency (L, Track D). B: channel isolation ADV-03 (M) | Native HDR swapchain, Ultra HDR gain maps, EXR, Adaptive HDR HEIC | A physically qualified HDR display and RUNBOOK; Linux/Docker stay SDR (Qt's GL swapchain is SDR-only) |
| F071 export presets | **GO** | Qt-only JPEG/PNG (WebP/TIFF after EN-02) sibling of D8; "ICC kept, EXIF/GPS/XMP removed" policy (L, Track D) | AVIF/JXL encoding; Lanczos3 until VIEW-05's resampler exists | AVIF/JXL encoders adopted for decode first (DEP-06/07) |
| F074 remote resize | **GO (hardening), DEFER resize** | F074a: `-AcceptSetDesktopSize=0`, narrowed `-AllowOverride` in `docker/start.py` (S, Track F, can join F1) | F074b resize-to-browser | Lead approves the RFB policy proxy; or Xvnc gains a size bound |
| F076 clipboard | **GO (policy slice), DEFER bridge** | F076a: explicit text-only clipboard through the noVNC panel, Xvnc limits, PRIMARY off, in-app routes for browser-reserved shortcuts, docs (M, Track F) | F076b per-client routing and browser system clipboard | RFB policy proxy; HTTPS made mandatory |
| F077 upload/download | **NO-GO for v1** (DEFER as separate project) | F077-0: document host file sharing into bind mounts (S, Track F) | Any in-container transfer service (XL) | Mandatory authenticated HTTPS front end, per-user credentials, F078 roles, F4 fuzzing and SYS-02 sandbox |
| F078 access modes | **GO (view-only password), DEFER rest** | F078a: second, server-enforced view-only VNC password (S-M, Track F) | F078b connection indicator from logs (M); F078c single-controller via proxy (L) | RFB policy proxy approved |

Dependencies (DEP-DECISIONS.md section 1): only **libjpeg-turbo 3.2.0** (D7) is proposed for Phase 2. XMP (C1) and hashing (B7) need no new library. libarchive arrives only with F039 slice B; libavif + dav1d and libjxl at M5 with FMT-04; LibRaw, libheif/libde265, OpenEXR/Imath and libultrahdr are deferred and pre-vetted.

## Findings the lead should see now

1. **Unbounded remote resize today (F074).** `docker/start.py` bounds `DESKTOP_SIZE` only at startup. Xvnc 1.13.1 accepts client SetDesktopSize by default up to 16384x16384 (about 1 GiB of framebuffer), and any authenticated noVNC user can switch on "Remote resizing". Xvnc has no option to cap requested sizes. F074a (`-AcceptSetDesktopSize=0`) is an S-size fix that also makes DOCKER.md's "resizing the browser does not change the server desktop" true unconditionally.
2. **Shared login lockout (F078).** Every client reaches Xvnc through websockify from 127.0.0.1, so failed-password back-off from one client blocks all clients.
3. **noVNC pin reason (F076, DEP-15).** Unreleased noVNC master reads the browser clipboard automatically in secure contexts. Do not move past 1.7.0 without review.
4. **One proxy, three slices.** F074b, F076b and F078c all need the same server-side RFB policy proxy (Python stdlib, between websockify and Xvnc). It is new security-sensitive protocol code; one decision covers all three.
5. **Codebase corrections to earlier assumptions.** Qt's JPEG/PNG/WebP writers keep ICC and drop EXIF (F071). Qt 6.8 already reads and writes 16-bit and float TIFF; the RGBA8 lines in `src/worker/main.cpp:205,252` and `src/render/image_canvas.cpp:97,122` are what block high precision (F048). Qt Quick 6.8 has an HDR swapchain request path on D3D11/D3D12; OpenGL (Linux, Docker) is SDR-only (F048). Qt's bundled libjpeg-turbo is a static internal archive and cannot be used for D7 (DEP-02). `QZipReader` is private and cannot bound decompression (F039).
6. **brotli security release** (2026-10-21, out-of-bounds write) matters for any future libjxl build: pin >= 1.2.1 (DEP-07).

## Verification

- Documents only: no build or `ctest` was run, because nothing compiled changed. `git diff --stat afeac1a..HEAD` lists only the files above.
- Facts come from upstream sources fetched in the session (cited in each document). Hosts that were unreachable from the cloud session (libjpeg-turbo.org, libraw.org, aomedia.org, packages.ubuntu.com, MDN, doc.qt.io, libarchive.org, 7-zip.org and others) are named, and facts that depend only on them are marked **unconfirmed** in the documents.
- Release signatures could not be listed for most GitHub-hosted projects (API blocked); every "signed releases" claim that is not cited is marked unconfirmed.
- Nothing was run in Docker, a browser or on Windows. Every behaviour claim about Xvnc, noVNC, Openbox or Qt comes from source/man pages at the pinned versions, not from a test. Items listed as "unconfirmed" in F074 (Openbox refit on resize), F076 (per-browser cancellable shortcuts), F078 (Xvnc auth logging) and DEP-DECISIONS section 4 need a probe in the real image.
- ui-design-skills: not applicable (no UI produced). The GO slices with UI (F039 A, F048 B, F071, F076a shortcut affordances) must load them when implemented.
- Research was done through three parallel subagents (local-media features, remote features, dependency facts); the session reviewed and edited their output and wrote DEP-DECISIONS and this handoff.

## Questions for the lead

1. Record go/no-go per item (table above). Requirement-row amendments implied by the split recommendations (for example F048 SDR versus HDR, F039 slice A versus B) go through REQUIREMENTS.md change control.
2. Approve or refuse the RFB policy proxy (unblocks F074b, F076b, F078c).
3. Fold F074a (and F078a) into Track F's F1 packet now, or a separate small packet?
4. F039: freeze a SOURCE-PROVIDER contract (`AssetId` gains an `entry`), and which track owns it (new Track G, or slice A in Track A and slice B in Track D)?
5. DEP-16 supply route: FetchContent + SHA256 for libjpeg-turbo now and vcpkg at M5, or vcpkg from the start?
6. DEP-01: accept own XMP writer with infoset-level preservation as meeting META-05?
7. DEP-05: HEIC deferred, or an opt-in build?
8. Qt stays at 6.8.3? (Only DEP-08, kimageformats, depends on it.)
9. Someone with an unrestricted network should confirm the unconfirmed provenance items in DEP-DECISIONS section 4 before the first pin.
