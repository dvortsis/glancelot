# MONITOR-CUSTOM-01 lead contract validation

This is a contract freeze, not feature implementation or qualification. The new coordinator declaration is unused until Claude supplies its source. PaneLayout gains two non-QML declarations only. The root CMake hook is guarded while source is absent; the lead must make it mandatory before final integration.

The installed Qt6.8.3 CMake macros were read to confirm qt_target_qml_sources accepts QML_FILES. The frozen header passed Qt moc and MSVC C++20 syntax checking, exit0. Actual commands after normal VS2022 x64 environment initialization:

```powershell
& .tools/Qt/6.8.3/msvc2022_64/bin/moc.exe include/glancelot/monitor_customization.hpp -o .cache/monitor-custom-contract/moc_monitor_customization.cpp
cl.exe /nologo /Zs /MD /EHsc /std:c++20 /Zc:__cplusplus /permissive- /I include /I .tools/Qt/6.8.3/msvc2022_64/include /I .tools/Qt/6.8.3/msvc2022_64/include/QtCore .cache/monitor-custom-contract/moc_monitor_customization.cpp
```

Actual header-check.txt output:

```text
moc_monitor_customization.cpp
```

header-check-status.txt records exit=0. This validates declaration/moc syntax only, not method definitions, linking, runtime behavior or the future active CMake fragment.

Optional whole-project configure checks used a copy of scripts/build.ps1 with the build command replaced by a return, without -Test. The first two accidentally overlapped in build-lead-monitor-custom-contract. Both known owned CMake processes were stopped. A subsequent single fresh configure in build-lead-monitor-custom-contract-serial also remained in configuration and was explicitly stopped; it does not count as success. All three partial logs are retained, including the script's configure-failed exception for interrupted attempts. No user/Claude process was stopped. No full Windows or Docker gate was run or claimed for an unimplemented packet. Claude and the integrator must provide actual complete feature gates later.

The lead handoff and frozen packet define complete acceptance, ownership and remaining validation. File boundaries and registration must be revisited through an explicit lead amendment if an actual implementation needs more sources or shared API.
