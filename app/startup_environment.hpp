#pragma once
#include "glancelot/settings_recovery.hpp"

namespace glancelot::startup {
struct OriginalConfig {
    bool useDefault{true};
    QString overrideDirectory;
};
// Capture before redirection; resolve defaults only after application metadata.
OriginalConfig captureOriginalConfig();
QString resolveOriginalConfig(const OriginalConfig&);
bool setConfigOverride(const QString&); // Unicode-preserving process-local setter
struct SafeEnvironment {
    recovery::Status status;
    recovery::Context context;
    QString requestedBackend; // a request, never proof of the actual RHI device
};
// Must precede QCore/QGuiApplication and all settings/session/theme/layout owners.
// Optional parent is a private test seam; the viewer uses the system TEMP root.
SafeEnvironment prepareSafeEnvironment(const QString& temporaryParent = {});
}
