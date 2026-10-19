#pragma once
#include <QString>

namespace glancelot {

class PlatformIntegration {
public:
    // Reveals the file or folder in the platform's native file manager (Explorer, etc.).
    // If path points to a file, highlights/selects the file inside its parent directory.
    // If path points to a directory, opens that directory.
    // Returns true if the operation was initiated successfully.
    static bool revealInFileManager(const QString& path);

    // Opens the platform's default terminal in the specified directory.
    // Windows: Windows Terminal (wt.exe) or PowerShell without shell string interpolation.
    // Linux: $TERMINAL / desktop terminal emulator.
    // Returns true if the terminal was initiated successfully.
    static bool openTerminal(const QString& directoryPath);
};

} // namespace glancelot
