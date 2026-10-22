# R12 L1 retirement bookkeeping correction

R12 identified that the queued retirement callback could run after deleteLater
clears the stored QPointer. removeAll(rawProcess) would then leave a null entry
until shutdown. The callback now prunes null entries and the matching live weak
pointer. Capturing a QPointer rather than a raw address also avoids confusing a
new allocation at the deleted process's address with the retired process.

Admission lifetime/release and the NotRunning requirement are unchanged. Public
headers, product semantics and UI are unchanged. No implementation-mirroring
test was added for this small bookkeeping change. Existing worker cancellation,
failure, shutdown and FS lease tests must run on the combined candidate.

Main's reviewed replacement fixture is synchronized through merge58fa807; the
old impersonating fixture sources are absent. Full Windows gates wait for the
current integrated fixture gate/security hold. Independent FS review and complete
combined Windows/Linux gates are pending; previous passes are not transferred to
this new source.
