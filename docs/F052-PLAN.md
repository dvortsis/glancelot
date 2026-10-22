# F052 delivery: assistive-technology audit

Lane Claude Code; branch `claude/f051`; reviewer from the Codex lane.

## Delivered
A regression audit, `input_ui_tests: everyInteractiveControlIsNamedForAssistiveTechnology`. With accessibility active it walks every visible, enabled interactive control (buttons, fields, combo boxes, switches, menu items, anything that takes Tab focus) on the main window and on each Settings page, asks the platform accessibility interface for it, and fails with the list of offenders if any has no accessible name or no role. It also fails if it inspects fewer than 40 controls, so an empty walk cannot pass. It found nothing to fix in the current UI, which already names its controls; the audit now keeps it that way, including the recorder, preset, high-contrast and context-menu controls added in this stack.

## Not done (unverified)
Real screen-reader trials (NVDA and Narrator on Windows, Orca on Linux) were not run and are reported as unverified. Live announcements for state changes (slideshow, chord pending, toasts use alert roles but their spoken output is untested), reading order, and table/tree semantics beyond role and name are open for those trials.
