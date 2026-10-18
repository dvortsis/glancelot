#pragma once
// Private app helper (not a public contract): the platform Trash/Recycle Bin backend behind FileOperationService.
// Exposed so tests can exercise the real backend on generated files and the Windows refusal policy directly.
#include "glancelot/file_operations.hpp"
#include <memory>
#include <string>

namespace platform_trash {
// Windows: IFileOperation with FOFX_RECYCLEONDELETE and a progress sink that refuses (E_FAIL from PreDeleteItem)
// any item the shell would delete instead of recycle. Elsewhere: QFile::moveToTrash (freedesktop.org trash; a rename,
// never a copy-and-delete). A success is only reported after the original path is verified gone.
std::unique_ptr<glancelot::TrashBackend> make();
// "Recycle Bin" on Windows, "Trash" elsewhere.
std::string trashName();
#ifdef _WIN32
// The PreDeleteItem decision: S_OK (0) only when the shell says the item will be recycled; otherwise E_FAIL.
long preDeleteDecision(unsigned long transferFlags);
#endif
}
