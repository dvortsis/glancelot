#include "glancelot/selection_export.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace glancelot {

namespace {

std::string sanitizeUtf8(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    const auto* p = reinterpret_cast<const unsigned char*>(sv.data());
    const size_t len = sv.size();
    size_t i = 0;
    while (i < len) {
        unsigned char c = p[i];
        if (c <= 0x7F) {
            out.push_back(static_cast<char>(c));
            ++i;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 < len && (p[i + 1] >= 0x80 && p[i + 1] <= 0xBF)) {
                out.push_back(static_cast<char>(c));
                out.push_back(static_cast<char>(p[i + 1]));
                i += 2;
            } else {
                out.append("\xEF\xBF\xBD");
                ++i;
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < len) {
                unsigned char c2 = p[i + 1];
                unsigned char c3 = p[i + 2];
                bool valid = false;
                if (c == 0xE0 && (c2 >= 0xA0 && c2 <= 0xBF) && (c3 >= 0x80 && c3 <= 0xBF)) valid = true;
                else if (((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) && (c2 >= 0x80 && c2 <= 0xBF) && (c3 >= 0x80 && c3 <= 0xBF)) valid = true;
                else if (c == 0xED && (c2 >= 0x80 && c2 <= 0x9F) && (c3 >= 0x80 && c3 <= 0xBF)) valid = true;
                if (valid) {
                    out.push_back(static_cast<char>(c));
                    out.push_back(static_cast<char>(c2));
                    out.push_back(static_cast<char>(c3));
                    i += 3;
                } else {
                    out.append("\xEF\xBF\xBD");
                    ++i;
                }
            } else {
                out.append("\xEF\xBF\xBD");
                ++i;
            }
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 < len) {
                unsigned char c2 = p[i + 1];
                unsigned char c3 = p[i + 2];
                unsigned char c4 = p[i + 3];
                bool valid = false;
                if (c == 0xF0 && (c2 >= 0x90 && c2 <= 0xBF) && (c3 >= 0x80 && c3 <= 0xBF) && (c4 >= 0x80 && c4 <= 0xBF)) valid = true;
                else if ((c >= 0xF1 && c <= 0xF3) && (c2 >= 0x80 && c2 <= 0xBF) && (c3 >= 0x80 && c3 <= 0xBF) && (c4 >= 0x80 && c4 <= 0xBF)) valid = true;
                else if (c == 0xF4 && (c2 >= 0x80 && c2 <= 0x8F) && (c3 >= 0x80 && c3 <= 0xBF) && (c4 >= 0x80 && c4 <= 0xBF)) valid = true;
                if (valid) {
                    out.push_back(static_cast<char>(c));
                    out.push_back(static_cast<char>(c2));
                    out.push_back(static_cast<char>(c3));
                    out.push_back(static_cast<char>(c4));
                    i += 4;
                } else {
                    out.append("\xEF\xBF\xBD");
                    ++i;
                }
            } else {
                out.append("\xEF\xBF\xBD");
                ++i;
            }
        } else {
            out.append("\xEF\xBF\xBD");
            ++i;
        }
    }
    return out;
}

std::string pathToUtf8(const fs::path& p, bool preferredSeparators = true) {
    fs::path target = preferredSeparators ? p.lexically_normal().make_preferred() : p.lexically_normal();
    auto u8 = target.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string formatModifiedIsoUtc(std::int64_t modified) {
    if (modified <= 0) return {};
    std::time_t sec = 0;
#ifdef _WIN32
    // Windows: FILETIME is in 100-nanosecond intervals since Jan 1, 1601 UTC.
    constexpr std::int64_t filetimeToUnixEpoch = 116444736000000000LL;
    if (modified < filetimeToUnixEpoch) return {};
    sec = static_cast<std::time_t>((modified - filetimeToUnixEpoch) / 10000000LL);
#else
    // POSIX: nanoseconds since Jan 1, 1970 UTC.
    sec = static_cast<std::time_t>(modified / 1000000000LL);
#endif
    std::tm tmUtc{};
#ifdef _WIN32
    if (gmtime_s(&tmUtc, &sec) != 0) return {};
#else
    if (gmtime_r(&sec, &tmUtc) == nullptr) return {};
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                  tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);
    return std::string(buf);
}

std::optional<std::string> computeRelativePath(const fs::path& itemPath, const fs::path& rootPath) {
    fs::path nItem = itemPath.lexically_normal();
    fs::path nRoot = rootPath.lexically_normal();

#ifdef _WIN32
    std::wstring itemStr = nItem.wstring();
    std::wstring rootStr = nRoot.wstring();
    if (itemStr.size() >= 2 && itemStr[1] == L':' && rootStr.size() >= 2 && rootStr[1] == L':') {
        if (towupper(itemStr[0]) != towupper(rootStr[0])) {
            return std::nullopt;
        }
        itemStr[0] = towupper(itemStr[0]);
        rootStr[0] = towupper(rootStr[0]);
        nItem = fs::path(itemStr);
        nRoot = fs::path(rootStr);
    }
#endif

    std::error_code ec;
    fs::path rel = fs::relative(nItem, nRoot, ec);
    if (ec || rel.empty()) {
        return std::nullopt;
    }

    auto u8 = rel.generic_u8string();
    std::string relStr(reinterpret_cast<const char*>(u8.data()), u8.size());

    if (relStr == ".." || relStr.starts_with("../") || rel.is_absolute()) {
        return std::nullopt;
    }
    return relStr;
}

bool arePathsEquivalent(const fs::path& p1, const fs::path& p2) {
    std::error_code ec;
    if (fs::exists(p1, ec) && fs::exists(p2, ec)) {
        if (fs::equivalent(p1, p2, ec)) return true;
    }
    fs::path n1 = p1.lexically_normal();
    fs::path n2 = p2.lexically_normal();
#ifdef _WIN32
    return _wcsicmp(n1.c_str(), n2.c_str()) == 0;
#else
    return n1 == n2;
#endif
}

} // namespace

std::string encodeCsvField(std::string_view utf8, bool neutralizeFormulas) {
    std::string s = sanitizeUtf8(utf8);
    if (neutralizeFormulas && !s.empty()) {
        char first = s.front();
        if (first == '=' || first == '+' || first == '-' || first == '@' || first == '\t' || first == '\r') {
            s.insert(s.begin(), '\'');
        }
    }
    bool needsQuotes = false;
    for (char ch : s) {
        if (ch == ',' || ch == '"' || ch == '\r' || ch == '\n') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return s;
    }
    std::string out;
    out.reserve(s.size() + 10);
    out.push_back('"');
    for (char ch : s) {
        if (ch == '"') {
            out.push_back('"');
            out.push_back('"');
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

ExportResult exportSelection(const ExportRequest& request, std::stop_token cancel,
                             const std::function<void(std::size_t, std::size_t)>& progress) {
    ExportResult result;
    result.generation = request.generation;

    // Early parameter validation
    if (request.destination.empty()) {
        result.status = ExportStatus::InvalidRequest;
        result.message = "Destination path is empty.";
        return result;
    }

    if (request.pathPolicy == PathPolicy::RelativeToRoot && request.root.empty()) {
        result.status = ExportStatus::InvalidRequest;
        result.message = "Root path is required for RelativeToRoot policy.";
        return result;
    }

    if (request.format == ExportFormat::Csv && request.columns.empty() && request.metadataColumns.empty()) {
        result.status = ExportStatus::InvalidRequest;
        result.message = "No CSV columns specified.";
        return result;
    }

    if (request.items.size() > request.maxItems) {
        result.status = ExportStatus::TooMany;
        result.message = "Item count exceeds maximum limit.";
        return result;
    }

    if (cancel.stop_requested()) {
        result.status = ExportStatus::Cancelled;
        result.message = "Export was cancelled before start.";
        return result;
    }

    fs::path normalDest = request.destination.lexically_normal();
    fs::path destDir = normalDest.parent_path();
    if (destDir.empty()) destDir = ".";

    std::error_code dirEc;
    if (!fs::exists(destDir, dirEc)) {
        result.status = ExportStatus::Failed;
        result.message = "Destination directory does not exist.";
        return result;
    }

    // Destination cannot be an exported item
    std::error_code destSnapEc;
    auto destSnap = snapshotTarget(normalDest, destSnapEc);
    for (const auto& item : request.items) {
        if (arePathsEquivalent(normalDest, item.path)) {
            result.status = ExportStatus::DestinationIsItem;
            result.message = "Destination path resolves to an exported item.";
            return result;
        }
        if (destSnap && (destSnap->identity.file != 0 || destSnap->identity.volume != 0)) {
            if (item.identity.file != 0 || item.identity.volume != 0) {
                if (item.identity == destSnap->identity) {
                    result.status = ExportStatus::DestinationIsItem;
                    result.message = "Destination identity matches an exported item.";
                    return result;
                }
            }
        }
    }

    // Destination exists check
    if (fs::exists(normalDest, destSnapEc) && !request.overwrite) {
        result.status = ExportStatus::DestinationExists;
        result.message = "Destination already exists and overwrite is false.";
        return result;
    }

    // Create unique temporary file in destination directory
    std::random_device rd;
    std::string tempFilename = "." + normalDest.filename().string() + ".tmp." +
                               std::to_string(rd()) + "_" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path tempFile = destDir / tempFilename;

    std::ofstream out(tempFile, std::ios::binary);
    if (!out.is_open()) {
        result.status = ExportStatus::Failed;
        result.message = "Failed to create temporary output file.";
        return result;
    }

    auto cleanupTemp = [&] {
        out.close();
        std::error_code rmEc;
        fs::remove(tempFile, rmEc);
    };

    // CSV UTF-8 BOM
    if (request.format == ExportFormat::Csv && request.utf8Bom) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    }

    // CSV Header Row
    if (request.format == ExportFormat::Csv) {
        std::string header;
        bool firstCol = true;
        for (ExportColumn col : request.columns) {
            if (!firstCol) header.push_back(',');
            firstCol = false;
            switch (col) {
            case ExportColumn::Path: header.append(encodeCsvField("path", false)); break;
            case ExportColumn::Name: header.append(encodeCsvField("name", false)); break;
            case ExportColumn::Folder: header.append(encodeCsvField("folder", false)); break;
            case ExportColumn::Kind: header.append(encodeCsvField("kind", false)); break;
            case ExportColumn::Bytes: header.append(encodeCsvField("bytes", false)); break;
            case ExportColumn::Modified: header.append(encodeCsvField("modified", false)); break;
            }
        }
        for (const auto& metaCol : request.metadataColumns) {
            if (!firstCol) header.push_back(',');
            firstCol = false;
            header.append(encodeCsvField(metaCol, request.neutralizeFormulas));
        }
        header.append("\r\n");
        out.write(header.data(), header.size());
        if (!out.good()) {
            cleanupTemp();
            result.status = ExportStatus::Failed;
            result.message = "Failed to write header to export file.";
            return result;
        }
    }

    std::vector<FileIdentity> writtenIdentities;
    std::vector<fs::path> writtenPaths;

    const std::size_t totalItems = request.items.size();
    for (std::size_t i = 0; i < totalItems; ++i) {
        if (cancel.stop_requested()) {
            cleanupTemp();
            result.status = ExportStatus::Cancelled;
            result.message = "Export was cancelled.";
            return result;
        }

        if (progress) {
            progress(i, totalItems);
        }

        const auto& item = request.items[i];
        ExportItem currentItem = item;

        // Revalidation
        if (request.revalidate) {
            std::error_code snapEc;
            auto currentSnap = snapshotTarget(item.path, snapEc);
            if (!currentSnap.has_value()) {
                result.skipped.push_back({item.path, ExportSkip::Missing});
                continue;
            }
            if ((item.identity.file != 0 || item.identity.volume != 0) &&
                currentSnap->identity != item.identity) {
                result.skipped.push_back({item.path, ExportSkip::Changed});
                continue;
            }
            if (item.kind == TargetKind::File) {
                if (currentSnap->bytes != item.bytes || currentSnap->modified != item.modified) {
                    result.skipped.push_back({item.path, ExportSkip::Changed});
                    continue;
                }
            }
            currentItem.identity = currentSnap->identity;
            currentItem.bytes = currentSnap->bytes;
            currentItem.modified = currentSnap->modified;
            currentItem.kind = currentSnap->kind;
        }

        // Deduplication
        bool duplicate = false;
        if (currentItem.identity.file != 0 || currentItem.identity.volume != 0) {
            for (const auto& id : writtenIdentities) {
                if (id == currentItem.identity) {
                    duplicate = true;
                    break;
                }
            }
        } else {
            for (const auto& p : writtenPaths) {
                if (arePathsEquivalent(currentItem.path, p)) {
                    duplicate = true;
                    break;
                }
            }
        }

        if (duplicate) {
            result.skipped.push_back({item.path, ExportSkip::Duplicate});
            continue;
        }

        // RelativeToRoot check
        std::string relPathStr;
        std::string relFolderStr;
        if (request.pathPolicy == PathPolicy::RelativeToRoot) {
            auto rel = computeRelativePath(currentItem.path, request.root);
            if (!rel.has_value()) {
                result.skipped.push_back({item.path, ExportSkip::OutsideRoot});
                continue;
            }
            relPathStr = *rel;
            auto relFolder = computeRelativePath(currentItem.path.lexically_normal().parent_path(), request.root);
            relFolderStr = relFolder.value_or(".");
        }

        // Format-specific row writing
        if (request.format == ExportFormat::PathList) {
            std::string pathOutput = (request.pathPolicy == PathPolicy::RelativeToRoot)
                                         ? relPathStr
                                         : pathToUtf8(currentItem.path, true);

            if (pathOutput.find('\r') != std::string::npos || pathOutput.find('\n') != std::string::npos) {
                result.skipped.push_back({item.path, ExportSkip::Unrepresentable});
                continue;
            }

            pathOutput.push_back('\n');
            out.write(pathOutput.data(), pathOutput.size());
        } else {
            // CSV row
            std::string row;
            bool firstCol = true;
            for (ExportColumn col : request.columns) {
                if (!firstCol) row.push_back(',');
                firstCol = false;
                switch (col) {
                case ExportColumn::Path: {
                    std::string pVal = (request.pathPolicy == PathPolicy::RelativeToRoot)
                                           ? relPathStr
                                           : pathToUtf8(currentItem.path, true);
                    row.append(encodeCsvField(pVal, request.neutralizeFormulas));
                    break;
                }
                case ExportColumn::Name: {
                    std::string nVal = pathToUtf8(currentItem.path.filename(), false);
                    row.append(encodeCsvField(nVal, request.neutralizeFormulas));
                    break;
                }
                case ExportColumn::Folder: {
                    std::string fVal = (request.pathPolicy == PathPolicy::RelativeToRoot)
                                           ? relFolderStr
                                           : pathToUtf8(currentItem.path.parent_path(), true);
                    row.append(encodeCsvField(fVal, request.neutralizeFormulas));
                    break;
                }
                case ExportColumn::Kind: {
                    std::string kVal = (currentItem.kind == TargetKind::Folder) ? "folder" : "file";
                    row.append(encodeCsvField(kVal, false));
                    break;
                }
                case ExportColumn::Bytes: {
                    std::string bVal = (currentItem.kind == TargetKind::File) ? std::to_string(currentItem.bytes) : "";
                    row.append(encodeCsvField(bVal, request.neutralizeFormulas));
                    break;
                }
                case ExportColumn::Modified: {
                    std::string mVal = formatModifiedIsoUtc(currentItem.modified);
                    row.append(encodeCsvField(mVal, request.neutralizeFormulas));
                    break;
                }
                }
            }

            for (const auto& metaCol : request.metadataColumns) {
                if (!firstCol) row.push_back(',');
                firstCol = false;
                std::string metaVal;
                for (const auto& [k, v] : currentItem.metadata) {
                    if (k == metaCol) {
                        metaVal = v;
                        break;
                    }
                }
                row.append(encodeCsvField(metaVal, request.neutralizeFormulas));
            }
            row.append("\r\n");
            out.write(row.data(), row.size());
        }

        if (!out.good()) {
            cleanupTemp();
            result.status = ExportStatus::Failed;
            result.message = "I/O error writing export record.";
            return result;
        }

        if (currentItem.identity.file != 0 || currentItem.identity.volume != 0) {
            writtenIdentities.push_back(currentItem.identity);
        } else {
            writtenPaths.push_back(currentItem.path.lexically_normal());
        }
        ++result.written;
    }

    if (progress) {
        progress(totalItems, totalItems);
    }

    out.flush();
    out.close();
    if (!out.good()) {
        cleanupTemp();
        result.status = ExportStatus::Failed;
        result.message = "Failed to flush and close temporary export file.";
        return result;
    }

    // Atomic move into final destination
    std::error_code renameEc;
#ifdef _WIN32
    if (request.overwrite && fs::exists(normalDest, renameEc)) {
        if (!MoveFileExW(tempFile.c_str(), normalDest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            DWORD err = GetLastError();
            cleanupTemp();
            result.status = ExportStatus::Failed;
            result.message = "Atomic replacement failed with Windows error " + std::to_string(err);
            return result;
        }
    } else {
        fs::rename(tempFile, normalDest, renameEc);
        if (renameEc) {
            cleanupTemp();
            result.status = ExportStatus::Failed;
            result.message = "Rename failed: " + renameEc.message();
            return result;
        }
    }
#else
    fs::rename(tempFile, normalDest, renameEc);
    if (renameEc) {
        cleanupTemp();
        result.status = ExportStatus::Failed;
        result.message = "Rename failed: " + renameEc.message();
        return result;
    }
#endif

    result.status = ExportStatus::Written;
    return result;
}

} // namespace glancelot

