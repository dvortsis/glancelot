#pragma once
#include "glancelot/recovery_checkpoint.hpp"
#include <QJsonObject>

namespace glancelot::recovery {
// F015, internal: reads a slot's stored tabs into `state`, whose folder/image are already set. Tab state is used only
// when it is a 2..MaxTabs array of {folder, image} valid absolute paths with an integral in-range activeTab whose record
// equals {folder, image}; anything else leaves one location. Shared by the claimed begin, the read-only fallback and
// the controller's legacy no-claim restore, so every path validates the same way.
void readTabs(const QJsonObject& windowRow, ResumeState& state);
}
