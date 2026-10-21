#include "glancelot/selection_export.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace glancelot;

namespace {
int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

void write(const fs::path& path, const std::string& text = "x") {
    std::ofstream(path, std::ios::binary) << text;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fs::path pathFromUtf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

struct Fixture {
    fs::path root;
    Fixture() {
        std::random_device random;
        root = fs::temp_directory_path() / ("glancelot-export-test-" + std::to_string(random()));
        fs::create_directories(root);
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path file(const std::string& name, const std::string& content = "test content") {
        auto p = root / pathFromUtf8(name);
        fs::create_directories(p.parent_path());
        write(p, content);
        return p;
    }
    fs::path dir(const std::string& name) {
        auto p = root / pathFromUtf8(name);
        fs::create_directories(p);
        return p;
    }
};

void testQuoting() {
    // 1. Quoting: commas, quotes, CR/LF, empty fields, a leading or trailing space kept.
    check(encodeCsvField("simple", false) == "simple", "simple field unchanged");
    check(encodeCsvField("a,b", false) == "\"a,b\"", "field with comma quoted");
    check(encodeCsvField("a\"b", false) == "\"a\"\"b\"", "field with quote doubled and quoted");
    check(encodeCsvField("a\r\nb", false) == "\"a\r\nb\"", "field with CRLF quoted");
    check(encodeCsvField("a\nb", false) == "\"a\nb\"", "field with LF quoted");
    check(encodeCsvField("a\rb", false) == "\"a\rb\"", "field with CR quoted");
    check(encodeCsvField("", false) == "", "empty field unchanged");
    check(encodeCsvField(" leading", false) == " leading", "leading space kept without comma/quote");
    check(encodeCsvField("trailing ", false) == "trailing ", "trailing space kept without comma/quote");
    check(encodeCsvField(" a,b ", false) == "\" a,b \"", "leading and trailing space kept inside quotes");
}

void testFormulaInjection() {
    // 2. Formula injection: all six prefixes (=, +, -, @, TAB, CR), with neutralization on and off.
    // A negative number such as -5 is still prefixed when on (documented behaviour).
    check(encodeCsvField("=SUM(1)", true) == "'=SUM(1)", "= neutralized");
    check(encodeCsvField("+123", true) == "'+123", "+ neutralized");
    check(encodeCsvField("-5", true) == "'-5", "-5 neutralized to '-5");
    check(encodeCsvField("@admin", true) == "'@admin", "@ neutralized");
    check(encodeCsvField("\tcol", true) == "'\tcol", "TAB neutralized");
    check(encodeCsvField("\rline", true) == "\"'\rline\"", "CR neutralized and quoted due to CR");

    // With neutralization off
    check(encodeCsvField("=SUM(1)", false) == "=SUM(1)", "= kept when off");
    check(encodeCsvField("+123", false) == "+123", "+ kept when off");
    check(encodeCsvField("-5", false) == "-5", "-5 kept when off");
    check(encodeCsvField("@admin", false) == "@admin", "@ kept when off");
    check(encodeCsvField("\tcol", false) == "\tcol", "TAB kept when off");
}

void testUnicode() {
    // 3. Unicode: CJK, emoji and RTL file names round-trip byte-exact. Invalid-UTF-8 metadata becomes U+FFFD.
    Fixture f;
    const std::string cjkName = "\xE6\xB8\xAC\xE8\xA9\xA6_\xE5\x9C\x96\xE7\x89\x87.png";      // 測試_圖片.png
    const std::string emojiName = "\xF0\x9F\x96\xBC\xEF\xB8\x8F_landscape.png";             // 🖼️_landscape.png
    const std::string rtlName = "\xD7\xAA\xD7\x9E\xD7\x95\xD7\xA0\xD7\x94.png";              // תמונה.png

    fs::path p1 = f.file(cjkName, "cjk");
    fs::path p2 = f.file(emojiName, "emoji");
    fs::path p3 = f.file(rtlName, "rtl");

    std::error_code ec;
    auto s1 = snapshotTarget(p1, ec);
    auto s2 = snapshotTarget(p2, ec);
    auto s3 = snapshotTarget(p3, ec);
    check(s1.has_value() && s2.has_value() && s3.has_value(), "snapshots succeeded");

    ExportRequest req;
    req.generation = 42;
    req.destination = f.root / "unicode_export.csv";
    req.columns = {ExportColumn::Name, ExportColumn::Bytes};
    req.metadataColumns = {"tag"};

    ExportItem item1{p1, TargetKind::File, s1->identity, s1->bytes, s1->modified, {{"tag", "cjk_tag"}}};
    ExportItem item2{p2, TargetKind::File, s2->identity, s2->bytes, s2->modified, {{"tag", "emoji_tag"}}};
    // Invalid UTF-8 in metadata
    std::string invalidUtf8 = "bad\xFF\xFEvalue";
    ExportItem item3{p3, TargetKind::File, s3->identity, s3->bytes, s3->modified, {{"tag", invalidUtf8}}};

    req.items = {item1, item2, item3};
    auto res = exportSelection(req);
    check(res.status == ExportStatus::Written, "unicode export written");
    check(res.written == 3, "wrote 3 rows");

    std::string content = readFile(req.destination);
    check(content.find(cjkName) != std::string::npos, "CJK roundtrips byte-exact");
    check(content.find(emojiName) != std::string::npos, "Emoji roundtrips byte-exact");
    check(content.find(rtlName) != std::string::npos, "RTL roundtrips byte-exact");
    check(content.find("\xEF\xBF\xBD") != std::string::npos, "invalid UTF-8 in metadata replaced with U+FFFD");
}

void testRelativeRoot() {
    // 4. Relative-root handling: inside, nested and outside the root, the root itself, and a sibling with a common prefix.
    Fixture f;
    fs::path rootDir = f.dir("root");
    fs::path insideFile = f.file("root/inside.png", "in");
    fs::path nestedFile = f.file("root/sub/nested.png", "nested");
    fs::path outsideFile = f.file("outside.png", "out");
    fs::path siblingPrefix = f.file("root_sibling/prefix.png", "sib");

    std::error_code ec;
    auto sInside = snapshotTarget(insideFile, ec);
    auto sNested = snapshotTarget(nestedFile, ec);
    auto sRoot = snapshotTarget(rootDir, ec);
    auto sOutside = snapshotTarget(outsideFile, ec);
    auto sSibling = snapshotTarget(siblingPrefix, ec);

    ExportRequest req;
    req.generation = 100;
    req.destination = f.root / "rel_export.csv";
    req.pathPolicy = PathPolicy::RelativeToRoot;
    req.root = rootDir;
    req.columns = {ExportColumn::Path};

    req.items = {
        ExportItem{insideFile, TargetKind::File, sInside->identity, sInside->bytes, sInside->modified},
        ExportItem{nestedFile, TargetKind::File, sNested->identity, sNested->bytes, sNested->modified},
        ExportItem{rootDir, TargetKind::Folder, sRoot->identity, 0, sRoot->modified},
        ExportItem{outsideFile, TargetKind::File, sOutside->identity, sOutside->bytes, sOutside->modified},
        ExportItem{siblingPrefix, TargetKind::File, sSibling->identity, sSibling->bytes, sSibling->modified}
    };

    auto res = exportSelection(req);
    check(res.status == ExportStatus::Written, "relative export written");
    check(res.written == 3, "wrote 3 items (inside, nested, root itself)");
    check(res.skipped.size() == 2, "skipped 2 items outside root");
    check(res.skipped[0].reason == ExportSkip::OutsideRoot, "outside is OutsideRoot");
    check(res.skipped[1].reason == ExportSkip::OutsideRoot, "sibling with common prefix is OutsideRoot");

    std::string content = readFile(req.destination);
    check(content.find("inside.png") != std::string::npos, "contains inside.png");
    check(content.find("sub/nested.png") != std::string::npos, "contains sub/nested.png with forward slashes");
    check(content.find("outside.png") == std::string::npos, "does not contain outside.png");
    check(content.find("prefix.png") == std::string::npos, "does not contain prefix.png");
}

void testRevalidation() {
    // 5. Revalidation: file deleted after snapshot (Missing), file replaced with same name (Changed), duplicate identity.
    // With revalidate=false, distinct paths that all have {0,0} identity are each written.
    Fixture f;
    fs::path p1 = f.file("del.png", "1");
    fs::path p2 = f.file("mod.png", "2");
    fs::path p3 = f.file("dup.png", "3");

    std::error_code ec;
    auto s1 = snapshotTarget(p1, ec);
    auto s2 = snapshotTarget(p2, ec);
    auto s3 = snapshotTarget(p3, ec);

    // Delete p1
    fs::remove(p1);

    // Replace p2 with different content/size
    write(p2, "replaced with longer content");

    ExportRequest req;
    req.destination = f.root / "reval_export.csv";
    req.revalidate = true;
    req.columns = {ExportColumn::Path};
    req.items = {
        ExportItem{p1, TargetKind::File, s1->identity, s1->bytes, s1->modified},
        ExportItem{p2, TargetKind::File, s2->identity, s2->bytes, s2->modified},
        ExportItem{p3, TargetKind::File, s3->identity, s3->bytes, s3->modified},
        ExportItem{p3, TargetKind::File, s3->identity, s3->bytes, s3->modified} // duplicate identity
    };

    auto res = exportSelection(req);
    check(res.status == ExportStatus::Written, "revalidation export written");
    check(res.written == 1, "only 1 written (p3)");
    check(res.skipped.size() == 3, "3 skipped");
    check(res.skipped[0].reason == ExportSkip::Missing, "deleted file skipped as Missing");
    check(res.skipped[1].reason == ExportSkip::Changed, "modified file skipped as Changed");
    check(res.skipped[2].reason == ExportSkip::Duplicate, "duplicate file skipped as Duplicate");

    // With revalidate = false, distinct paths with {0,0} identity are each written
    ExportRequest reqNoReval;
    reqNoReval.destination = f.root / "no_reval.csv";
    reqNoReval.revalidate = false;
    reqNoReval.columns = {ExportColumn::Path};
    reqNoReval.items = {
        ExportItem{f.root / "unknown1.png", TargetKind::File, {0, 0}, 10, 100},
        ExportItem{f.root / "unknown2.png", TargetKind::File, {0, 0}, 20, 200},
        ExportItem{f.root / "unknown3.png", TargetKind::File, {0, 0}, 30, 300}
    };
    auto resNoReval = exportSelection(reqNoReval);
    check(resNoReval.status == ExportStatus::Written, "no-reval written");
    check(resNoReval.written == 3, "all 3 distinct {0,0} items written");
}

void testDestination() {
    // 6. Destination: refuse existing file without overwrite, replace with overwrite, refuse destination when it is
    // one of the items even with overwrite, and refuse destination directory that doesn't exist (Failed).
    Fixture f;
    fs::path f1 = f.file("item1.png", "item1");
    fs::path existingDest = f.file("exists.csv", "old content");

    std::error_code ec;
    auto s1 = snapshotTarget(f1, ec);

    ExportRequest req;
    req.destination = existingDest;
    req.overwrite = false;
    req.items = {ExportItem{f1, TargetKind::File, s1->identity, s1->bytes, s1->modified}};
    req.columns = {ExportColumn::Path};

    auto resExists = exportSelection(req);
    check(resExists.status == ExportStatus::DestinationExists, "refuse existing destination without overwrite");
    check(readFile(existingDest) == "old content", "existing destination unchanged");

    // Replace with overwrite = true
    req.overwrite = true;
    auto resReplace = exportSelection(req);
    check(resReplace.status == ExportStatus::Written, "replace existing with overwrite");
    check(readFile(existingDest) != "old content", "destination overwritten");

    // Destination is one of the items (refused even with overwrite)
    req.destination = f1;
    req.overwrite = true;
    auto resItem = exportSelection(req);
    check(resItem.status == ExportStatus::DestinationIsItem, "refuse destination when it is an exported item");
    check(readFile(f1) == "item1", "item file unchanged");

    // Destination directory does not exist
    req.destination = f.root / "nonexistent" / "subdir" / "out.csv";
    auto resFailed = exportSelection(req);
    check(resFailed.status == ExportStatus::Failed, "refuse nonexistent destination directory");
}

void testCancellation() {
    // 7. Cancellation before and mid-export (use stop source triggered from progress).
    // There must be no destination file and no temp file left over, and an earlier destination file must be unchanged.
    Fixture f;
    fs::path f1 = f.file("c1.png", "1");
    fs::path f2 = f.file("c2.png", "2");
    fs::path f3 = f.file("c3.png", "3");

    std::error_code ec;
    auto s1 = snapshotTarget(f1, ec);
    auto s2 = snapshotTarget(f2, ec);
    auto s3 = snapshotTarget(f3, ec);

    fs::path dest = f.file("dest.csv", "original destination content");

    // Cancel before export
    std::stop_source ssBefore;
    ssBefore.request_stop();

    ExportRequest req;
    req.destination = f.root / "never_created.csv";
    req.items = {
        ExportItem{f1, TargetKind::File, s1->identity, s1->bytes, s1->modified},
        ExportItem{f2, TargetKind::File, s2->identity, s2->bytes, s2->modified}
    };
    req.columns = {ExportColumn::Path};

    auto resBefore = exportSelection(req, ssBefore.get_token());
    check(resBefore.status == ExportStatus::Cancelled, "cancelled before export");
    check(!fs::exists(req.destination), "no destination created on pre-cancel");

    // Mid-export cancellation
    std::stop_source ssMid;
    req.destination = dest;
    req.overwrite = true;
    req.items = {
        ExportItem{f1, TargetKind::File, s1->identity, s1->bytes, s1->modified},
        ExportItem{f2, TargetKind::File, s2->identity, s2->bytes, s2->modified},
        ExportItem{f3, TargetKind::File, s3->identity, s3->bytes, s3->modified}
    };

    auto progress = [&](std::size_t done, std::size_t total) {
        if (done >= 1) {
            ssMid.request_stop();
        }
    };

    auto resMid = exportSelection(req, ssMid.get_token(), progress);
    check(resMid.status == ExportStatus::Cancelled, "cancelled mid export");
    check(readFile(dest) == "original destination content", "original destination file unchanged");

    // Check no temp files left behind
    for (const auto& entry : fs::directory_iterator(f.root)) {
        std::string filename = entry.path().filename().string();
        check(filename.find(".tmp.") == std::string::npos, "no temp file left behind");
    }
}

void testBoundsAndGeneration() {
    // 8. maxItems exceeded writes nothing, and the generation is echoed on every status.
    Fixture f;
    fs::path f1 = f.file("b1.png", "1");
    fs::path f2 = f.file("b2.png", "2");

    std::error_code ec;
    auto s1 = snapshotTarget(f1, ec);
    auto s2 = snapshotTarget(f2, ec);

    ExportRequest req;
    req.generation = 9999;
    req.destination = f.root / "bounds.csv";
    req.maxItems = 1;
    req.items = {
        ExportItem{f1, TargetKind::File, s1->identity, s1->bytes, s1->modified},
        ExportItem{f2, TargetKind::File, s2->identity, s2->bytes, s2->modified}
    };
    req.columns = {ExportColumn::Path};

    auto res = exportSelection(req);
    check(res.status == ExportStatus::TooMany, "status TooMany when exceeding maxItems");
    check(res.generation == 9999, "generation echoed on TooMany");
    check(!fs::exists(req.destination), "nothing written on TooMany");

    // Test generation on all other statuses
    req.destination = "";
    check(exportSelection(req).generation == 9999, "generation echoed on InvalidRequest");

    req.destination = f.root / "bounds.csv";
    req.items = {};
    req.columns = {};
    check(exportSelection(req).generation == 9999, "generation echoed on no columns");
}

void testPathList() {
    // 9. PathList output: LF endings, no BOM, CR/LF in a path gives Unrepresentable.
    Fixture f;
    fs::path f1 = f.file("path1.png", "1");
    fs::path f2 = f.file("path2.png", "2");

    std::error_code ec;
    auto s1 = snapshotTarget(f1, ec);
    auto s2 = snapshotTarget(f2, ec);

    ExportRequest req;
    req.destination = f.root / "paths.txt";
    req.format = ExportFormat::PathList;
    req.utf8Bom = true; // should be ignored for PathList
    req.items = {
        ExportItem{f1, TargetKind::File, s1->identity, s1->bytes, s1->modified},
        ExportItem{f2, TargetKind::File, s2->identity, s2->bytes, s2->modified},
        ExportItem{f.root / "unrep\npath.png", TargetKind::File, {0, 0}, 0, 0}
    };

    req.revalidate = false;
    auto res = exportSelection(req);
    check(res.status == ExportStatus::Written, "PathList export written");
    check(res.written == 2, "wrote 2 valid paths");
    check(res.skipped.size() == 1, "skipped 1 unrepresentable path");
    check(res.skipped[0].reason == ExportSkip::Unrepresentable, "skipped reason is Unrepresentable");

    std::string content = readFile(req.destination);
    // Check no BOM
    check(!content.starts_with("\xEF\xBB\xBF"), "PathList has no UTF-8 BOM");
    // Check LF line endings (no \r\n)
    check(content.find("\r\n") == std::string::npos, "PathList has LF endings only");
    check(content.ends_with("\n"), "PathList ends with LF");
}

} // namespace

int main() {
    std::cout << "Running selection_export tests...\n";
    testQuoting();
    testFormulaInjection();
    testUnicode();
    testRelativeRoot();
    testRevalidation();
    testDestination();
    testCancellation();
    testBoundsAndGeneration();
    testPathList();

    if (failures > 0) {
        std::cerr << "FAILED with " << failures << " error(s)\n";
        return 1;
    }
    std::cout << "All selection_export tests PASSED!\n";
    return 0;
}

