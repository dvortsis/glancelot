# Monitor fix review and core correction, October 21, 2026

Intake pin `0e32d1617ac6f7cbf1f0abdd79b9fdbea2eb9b84` (`agent/monitor-output`), source fix `27bc47c`, predecessor `ae1b7e4`. Reviewed against the five findings in `MONITOR-OUTPUT-01-ae1b7e4-CODEX-REVIEW.md`. Codex worktree `.worktrees/codex-monitor-output-r2`, branch `codex/monitor-output-r2`, build `build-codex-monitor-output-r2`. No edits to Gemini/Claude worktrees; no source merge to main.

## Assessment of the submitted candidate

The hidden-secondary ownership, comparison generation high-water, invalid remembered fit and chord acceptance corrections address the four concrete defects. Their four regression tests remain in the suite. Two issues remained:

- **P1, scope:** deferring all output viewport/color/scale contradicted main's explicit October21 scope decision. User explicitly chose implementation in this round; lead froze additive interfaces in `docs/MONITOR-OUTPUT-01-CORE-CORRECTION.md` on main `c534c31`, under the integration lock. The old handoff's deferral is not accepted as a requirement waiver.
- **P2, chord lifecycle:** an unfinished dismissal chord had no deadline and could survive a binding change. Auto-repeat could also complete a repeated-key sequence. Corrected below; focus/hide cancellation remains.

## Codex implementation

Initial source commit `6a08c3e16573b028a39808d9336f58221af0c6d2`:

- Session viewport state keyed by display identity, retained across close/reopen, navigation and retarget. State is bounded to64 identities. Fit and actual-size modes, zoom and source-coordinate center use the presentation screen's physical DPR; controller canvas is untouched.
- Linked comparison uses a common physical zoom and normalized source center. Independent mode permits separate pointer operation and registered keyboard commands for either viewport. New commands have a per-task registry block, user bindings, controller dispatch and clean-window dispatch. No new controller chrome appears on output. Advanced picker, named presets, workspace layout and cross-restart customization remain the separate packet.
- Shared sRGB working pixels stay unchanged. One fixed33-cube RGB atlas (1089x33 RGBA8,143748bytes) maps working pixels to the SDR output profile in the GPU shader. Two output canvases share the CPU atlas; no full-size converted images or duplicate decodes. A separately exposed conservative4MiB color reservation covers atlas/render/upload resources; the original256MiB decoded-image and256MiB aggregate presentation texture budgets remain.
- A single process-wide worker permit prevents accumulation of profile jobs across repeated window lifecycles. Latest revisions supersede older requests; worker captures only bounded values, never a window/controller or user media. ICC files are limited to4MiB; no directory scan. Windows queries a screen-specific DC and local fixed-drive profile; unavailable/invalid profiles explicitly report sRGB fallback. Profile changes are checked while output is visible. Other platforms currently use the explicit unavailable-profile fallback; native profile adapters there and physical ICC/HDR accuracy are not claimed.
- Dismissal/command chords expire at1500ms, consult the current binding, ignore auto-repeat, cancel on focus loss/hide, and give Escape a cancellation step before dismissal. No existing timeout or assertion was relaxed.

The Windows DC/profile lookup was checked against primary API documentation: [CreateDCW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdcw) identifies a particular display; [GetICMProfileW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-geticmprofilew) reads that DC's output-profile filename and reports the required buffer size. All DC operations and profile reads occur on the worker, with DC destruction on the same thread.

## Actual verification so far

Raw outputs, including failures, are in [monitor-output-r2](monitor-output-r2/).

- The initial authoring build ran while interface/method definitions were incomplete and reported four unresolved mouse/wheel methods. Subsequent complete-source targeted builds passed; this is retained as authoring evidence, not a candidate runtime failure.
- Expanded QtTest suite:35 passed,0 failed,1 opt-in native proof skipped,1880ms, exit0 (`windows-core-cases.txt`). Covers original regressions plus viewport restoration/navigation, linked/independent comparison, physical-scale arithmetic, bounded/latest profile transitions,125 generated SDR reference nodes, chord timeout/rebind/focus/auto-repeat and registered output-key dispatch.
- Native Windows D3D shader readback with software-adapter preference:3 passed,0 failed,536ms, exit0. A generated Display-P3 patch exactly matched Qt's color-conversion reference at the captured center pixel. This proves that patch's shader path, not monitor calibration or input-to-photon latency (`windows-native-color-proof.txt`).
- Separate `QT_SCALE_FACTOR=2` process:3 passed,0 failed; actual zoom1 produced2 source pixels per logical coordinate pixel, as required for physical1:1 (`windows-dpr2-proof.txt`). This is controlled scale arithmetic, not a physical display-transition qualification.

Commands:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/monitor-output-r2 -Run { & ./scripts/build.ps1 -BuildDir build-codex-monitor-output-r2 -Test; docker build --progress=plain -t glancelot-browser:codex-monitor-output-r2 . }
# Actual orchestration separately records each native exit/exception and checks Docker exit explicitly.
# Targeted runs, Qt bin on PATH:
& ./build-codex-monitor-output-r2/monitor_output_tests.exe -o docs/verification/monitor-output-r2/windows-core-cases.txt,txt
# QT_QPA_PLATFORM=windows; GLANCELOT_MONITOR_NATIVE_COLOR_PROOF=1;
# QSG_RHI_BACKEND=d3d11; QSG_RHI_PREFER_SOFTWARE_RENDERER=1:
& ./build-codex-monitor-output-r2/monitor_output_tests.exe testNativeGpuColorPatch -o docs/verification/monitor-output-r2/windows-native-color-proof.txt,txt
```

Full gates are running serially through the main checkout's gate-lock; pending is not passed. Actual Sonnet cross-lane review is mandatory for Codex-authored changes and awaits the configured service's noon Denver quota reset. No Codex self-approval and no integration approval. Physical monitor calibration, ICC hardware accuracy, HDR, input-to-photon, suspend/hotplug hardware, sanitizer and GPU-loss recovery remain unverified unless later evidence explicitly establishes them.

Native UI design review: clean output is preserved; keyboard commands supplement pointer operations and either independent comparison viewport is reachable by command. No new spacing, type, palette, icon family or motion tokens. Highest-leverage verification is a native cross-display state restore, because controlled DPR arithmetic alone cannot establish it.

## Follow-up corrections and qualification

Full Windows on6a08c3e passed33/33,494.36s,exit0. Docker canceledcontexttransferexit130 beforecompile; rawretained. Independentedgeprobes reproduced lostreferencezoomandmissingcolorstatusclose-notification (2failed). NativeattachedDPR1/2retargetproofpassed onoriginal6a product; the suspectedDPRrestore defectwasnotreproduced. Outputprofile reported P27h-20.

Commitsa0ba770,c84ea83,51fb9c6 retain metadata-only referenceviewport while itsimageisabsent, normalize sourcecenters once via snapshots carrying source dimensions, initializeDPRbookkeeping onmanualrestore, boundfitstate64, and notifyoutputcolor consumersatrefresh/open/close. The first correctionintroduced fitrestore regressions; actualfailedrunsare retained in windows-edge-corrected-cases.txt and windows-edge-final-cases.txt. Initializingfitfromtargetidentity beforeliverestore fixedthem without changingassertions.

Finaltargeted51fb9c6:37passed,0failed,2nativeopt-in-skipped,1975ms,exit0 (windows-restored-fit-cases.txt). Separatefinalnativeproof:4passed,0failed,947ms,exit0 (windows-native-final.txt): DisplayP3shaderpatch exactQtreference; physicalattachedDPR1and2 savedzoom/sourcecenters restoredexactly, nativeprofileP27h-20. This isviewporttransitionandgeneratedshader evidence; physicalICCcoloraccuracy/HDR/suspend/hotplug remainunverified. FreshfullWindows/Docker gates on51fb9c6 arepending, independentactualSonnetreviewrunning. No productmerged.

## Independent required fixes

ActualSonnet5.5 review session842f8174-64ec-4369-bba1-a482257ae935,36turns,202233ms,exit0 requestedfreshdisplaydefaults,fullsingle-canvastextureadmissionandclosedoutputhistoryrelease. RawJSONanduneditedCLAUDE-REVIEW.mdretained. Itreadall9requiredUIskills. Sourcecommit9b03718a59533bacc7b1f66b1536af409ffa3d28 resolvesallthree withnewregressions: defaultlinked/shrinkresetbeforeeditingnewdisplay,generated150MiBimagebudgetadmissionandmodecaps,closednavigationretainscurrent+pinnedonly. Optionaladvisoriesresolved:periodicprofilededuplication/norevisionchurnwhileinflight,16MiBconservativecolorreservationincludingworkerinput/ICCtransients,documentedsharedselectiongeneration,defaultCtrl+Alt+Foutputrecoveryfitbinding. QHashbounded64evictionisarbitrarynotLRU; nativeprofilediscoveryonWindowsdoesnotqualifyphysicalcalibrationorHDR.

The intermediate51fb9c6 fullgatewasexplicitlyinterruptedwhensourcereviewrequiredchanges; partialWindowsoutputretained,notqualified. NoDockerstepcompletedforthatinterruptedgate. Fresh9b03718fullWindows/Docker nowrunningthroughmainlockwithnativeenvironmentoverridesremovedbeforefullgate.

9b03718targeted:40passed,0failed,2nativeopt-inskipped,2108ms,exit0 (windows-sonnet-fixes-cases.txt). Nativefinalafterreviewfixes:4passed,0failed,782ms,exit0; generatedDisplayP3patchmatchesQtreferenceandexactattachedDPR1/2viewrestore,firstvisitdefaultschecked (windows-native-reviewed-fixes.txt). IndependentSonnetconfirmationpending.

## Source confirmation

Actual Claude Sonnet5.5 follow-up ACCEPTED source9b03718 with no required changes, session9b6ceafa-6ec9-4f52-8be0-f2bc7c5b3670,29turns,144343ms,exit0. Allnine requiredUIskillswere read. Unedited report CLAUDE-FOLLOWUP.md and JSON/prompt/status retained. It confirmed fresh-display defaults, mode-aware256/0or128/128MiBtexturebudgets and no closed-navigation history. Sourceacceptanceisnotfullgateorhardwareapproval. Nonblockingadvisoriesinclude arbitraryboundedQHasheviction, unboundedpanwithfitrecoverycommand, firstkeychorddeadline, single-generationcontractandacceptedstale-pincounterlimitations. Fullgatequalificationremains pending.

## Completed qualification on the reviewed source

Source9b03718 completed fresh full Windows33/33 in450.91seconds and LinuxDocker33/33 in232.74seconds, both exit0. Actual outputs: windows-reviewed-full.txt and docker-reviewed-full.txt, with exitstatusfiles. The monitor target passed2.04s Windows and1.81s Linux. Both full commands ran in one main gate-lock block; the lock is nowfree. Product/test files remain byte-identical to9b03718. Earlierfailed/canceled/interruptedruns remainarchived. This qualifies the branch candidate only; main synchronization, conflictresolution/independentconfirmation and integrated-commit gates remain separate. No source merge to main or task-done claim.
