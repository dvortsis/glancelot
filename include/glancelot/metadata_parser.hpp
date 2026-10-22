#pragma once
#include "metadata_model.hpp"
#include <QString>

namespace glancelot {

class MetadataParser {
public:
    // Parses metadata from file bounded by maxReadBytes.
    // Safe against corrupted, malformed, or cyclic IFD structures.
    static MetadataRecord parse(const QString& filePath, quint64 generation = 0, qint64 maxReadBytes = 256 * 1024);
};

} // namespace glancelot

