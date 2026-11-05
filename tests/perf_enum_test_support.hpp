#pragma once
// Shared by perf_enum_tests and perf_enum_ui_tests. Only targets defining GLANCELOT_PERF_ENUM_TESTING can use it.
#include "directory_scan.hpp"
#include <QScopeGuard>
#include <QString>
#include <atomic>
#include <memory>
#include <thread>

namespace {
// A synthetic source for the private enumeration seam: what a slow, failing or huge folder looks like, on demand.
// `name` gives the entries in arrival order. After `heldAfter` entries it waits for release() or the stop token.
struct FakeFolder {
    std::function<QString(int)> name = [](int i) { return QString("f%1.png").arg(i, 7, 10, QChar('0')); };
    int total{}, heldAfter{-1}, failAfter{-1};
    std::shared_ptr<std::atomic_int> emitted = std::make_shared<std::atomic_int>(0), starts = std::make_shared<std::atomic_int>(0), exits = std::make_shared<std::atomic_int>(0);
    std::shared_ptr<std::atomic_bool> released = std::make_shared<std::atomic_bool>(false);
    void release() const { released->store(true); }
    directory_scan::Enumerator enumerator() const {
        return [self = *this](const std::filesystem::path& directory, bool, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            ++*self.starts;
            const auto leave = qScopeGuard([&] { ++*self.exits; });
            directory_scan::Outcome outcome;
            for (int i = 0; i < self.total; ++i) {
                if (cancel.stop_requested()) { outcome = {}; outcome.cancelled = true; return outcome; }
                if (i == self.failAfter) { outcome = {}; outcome.accessible = false; return outcome; }
                if (i == self.heldAfter) {
                    while (!self.released->load() && !cancel.stop_requested()) std::this_thread::yield();
                    if (cancel.stop_requested()) { outcome = {}; outcome.cancelled = true; return outcome; }
                }
                if (std::size_t(i) >= limit) { outcome.truncated = true; return outcome; }
                glancelot::DirectoryEntry entry;
                const QString name = self.name(i);
                entry.path = directory / directory_scan::nativePath(name);
                entry.kind = name.startsWith("dir") ? glancelot::EntryKind::Folder : glancelot::EntryKind::Image;
                entry.meta = {std::int64_t(i), std::uintmax_t(i), true};
                if (!sink(std::move(entry))) { outcome = {}; outcome.cancelled = true; return outcome; }
                ++*self.emitted;
            }
            return outcome;
        };
    }
};
// Installs a source for the models constructed while it is alive. Patience off: rows arrive as soon as they exist.
struct SeamScope {
    explicit SeamScope(const FakeFolder* folder, int batchRows = 64) {
        directory_scan::testSeam = {};
        directory_scan::testSeam.tuning.patienceMs = 0; directory_scan::testSeam.tuning.minArrivalRows = 1;
        directory_scan::testSeam.tuning.batchRows = batchRows; directory_scan::testSeam.tuning.flushMs = 60000;
        if (folder) directory_scan::testSeam.enumerator = folder->enumerator();
    }
    ~SeamScope() { directory_scan::testSeam = {}; }
};
}
