#include "glancelot/io_scheduler.hpp"
#include "glancelot/thumbnail_service.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#ifndef GLANCELOT_DECODER_PATH
#define GLANCELOT_DECODER_PATH ""
#endif

using namespace glancelot;

class IoSchedulerTests : public QObject {
    Q_OBJECT
private slots:
    void init() {
        IoScheduler::instance().reset();
    }

    void testDetectStorageTypeWithOverrides() {
        auto& sched = IoScheduler::instance();
        
        // UNC paths automatically detect as Network
        QCOMPARE(sched.detectStorageType("//nas/photos/img.jpg"), StorageDeviceType::Network);
        QCOMPARE(sched.detectStorageType("\\\\server\\share\\photo.png"), StorageDeviceType::Network);

        // Path overrides for test fixtures
        sched.setPathOverride("/mnt/fast_nvme", StorageDeviceType::Ssd);
        sched.setPathOverride("/mnt/archive_hdd", StorageDeviceType::Rotational);
        sched.setPathOverride("/mnt/smb_share", StorageDeviceType::Network);

        QCOMPARE(sched.detectStorageType("/mnt/fast_nvme/pic.jpg"), StorageDeviceType::Ssd);
        QCOMPARE(sched.detectStorageType("/mnt/archive_hdd/sub/pic.jpg"), StorageDeviceType::Rotational);
        QCOMPARE(sched.detectStorageType("/mnt/smb_share/test.png"), StorageDeviceType::Network);

        sched.clearPathOverrides();
        // After clearing overrides, /mnt/fast_nvme falls back to default
        QCOMPARE(sched.detectStorageType("/mnt/fast_nvme/pic.jpg"), StorageDeviceType::Ssd);
    }

    void testManualProfileOverride() {
        auto& sched = IoScheduler::instance();
        sched.setPathOverride("/mnt/hdd", StorageDeviceType::Rotational);
        QCOMPARE(sched.detectStorageType("/mnt/hdd/img.jpg"), StorageDeviceType::Rotational);
        QCOMPARE(sched.effectiveStorageType("/mnt/hdd/img.jpg"), StorageDeviceType::Rotational);

        // Force manual SSD profile
        sched.setProfile(StorageProfile::Ssd);
        QCOMPARE(sched.profile(), StorageProfile::Ssd);
        QCOMPARE(sched.effectiveStorageType("/mnt/hdd/img.jpg"), StorageDeviceType::Ssd);

        // Force manual Network profile
        sched.setProfile(StorageProfile::Network);
        QCOMPARE(sched.profile(), StorageProfile::Network);
        QCOMPARE(sched.effectiveStorageType("/mnt/hdd/img.jpg"), StorageDeviceType::Network);

        // Revert to Auto
        sched.setProfile(StorageProfile::Auto);
        QCOMPARE(sched.profile(), StorageProfile::Auto);
        QCOMPARE(sched.effectiveStorageType("/mnt/hdd/img.jpg"), StorageDeviceType::Rotational);
    }

    void testConcurrencyLimitsAndDeadlinesByProfile() {
        auto& sched = IoScheduler::instance();
        sched.setPathOverride("/fast", StorageDeviceType::Ssd);
        sched.setPathOverride("/slow", StorageDeviceType::Rotational);
        sched.setPathOverride("/remote", StorageDeviceType::Network);

        // SSD allows up to 2 workers, 5000ms deadline, prefetch allowed
        QCOMPARE(sched.maxConcurrentBackgroundWorkers("/fast/1.jpg"), 2);
        QCOMPARE(sched.recommendedThumbnailDeadlineMs("/fast/1.jpg"), 5000);
        QVERIFY(sched.isPrefetchPermitted("/fast/1.jpg"));

        // Rotational limits to 1 worker, 8000ms deadline
        QCOMPARE(sched.maxConcurrentBackgroundWorkers("/slow/1.jpg"), 1);
        QCOMPARE(sched.recommendedThumbnailDeadlineMs("/slow/1.jpg"), 8000);

        // Network limits to 1 worker, 3000ms deadline, prefetch disabled
        QCOMPARE(sched.maxConcurrentBackgroundWorkers("/remote/1.jpg"), 1);
        QCOMPARE(sched.recommendedThumbnailDeadlineMs("/remote/1.jpg"), 3000);
        QVERIFY(!sched.isPrefetchPermitted("/remote/1.jpg"));
    }

    void testPriorityAdmissionAndPreemption() {
        auto& sched = IoScheduler::instance();
        sched.setPathOverride("/test", StorageDeviceType::Ssd);

        // Admit 2 background thumbnail jobs on SSD
        QVERIFY(sched.tryAdmit(101, JobPriority::Thumbnail, 1024, "/test/1.jpg"));
        QVERIFY(sched.tryAdmit(102, JobPriority::Thumbnail, 1024, "/test/2.jpg"));
        QCOMPARE(sched.activeJobCount(), 2);

        // 3rd background job on SSD exceeds max background workers (2)
        QVERIFY(!sched.tryAdmit(103, JobPriority::Thumbnail, 1024, "/test/3.jpg"));

        // Foreground decode ALWAYS admits regardless of background count
        QVERIFY(sched.tryAdmit(1, JobPriority::Foreground, 1024, "/test/hero.jpg"));
        QCOMPARE(sched.activeJobCount(), 3);
        QCOMPARE(sched.activeForegroundJobs(), 1);

        // Speculative prefetch is denied while foreground decode is running
        QVERIFY(!sched.isPrefetchPermitted("/test/ahead.jpg"));
        QVERIFY(!sched.tryAdmit(201, JobPriority::Prefetch, 1024, "/test/ahead.jpg"));

        // Release foreground decode
        sched.release(1);
        QCOMPARE(sched.activeForegroundJobs(), 0);
        QCOMPARE(sched.activeJobCount(), 2);

        // Release one background job
        sched.release(101);
        QCOMPARE(sched.activeJobCount(), 1);

        // Now prefetch is allowed
        QVERIFY(sched.isPrefetchPermitted("/test/ahead.jpg"));
        QVERIFY(sched.tryAdmit(201, JobPriority::Prefetch, 1024, "/test/ahead.jpg"));
        QCOMPARE(sched.activeJobCount(), 2);
    }

    void testRotationalSerialization() {
        auto& sched = IoScheduler::instance();
        sched.setPathOverride("/disk", StorageDeviceType::Rotational);

        // Rotational only allows 1 background worker to avoid head thrashing
        QVERIFY(sched.tryAdmit(10, JobPriority::Thumbnail, 512, "/disk/1.jpg"));
        QVERIFY(!sched.tryAdmit(11, JobPriority::Thumbnail, 512, "/disk/2.jpg"));

        // Rotational prefetch denied if any job active
        QVERIFY(!sched.isPrefetchPermitted("/disk/next.jpg"));

        sched.release(10);
        QCOMPARE(sched.activeJobCount(), 0);
        QVERIFY(sched.isPrefetchPermitted("/disk/next.jpg"));
    }

    void testMemoryBudgetCeiling() {
        auto& sched = IoScheduler::instance();
        sched.setMemoryLimit(10000); // 10,000 bytes limit

        QVERIFY(sched.tryAdmit(1, JobPriority::Thumbnail, 6000, "/local/1.jpg"));
        QCOMPARE(sched.admittedBytes(), 6000);

        // Second job requests 5000 bytes: 6000 + 5000 > 10000 -> rejected
        QVERIFY(!sched.tryAdmit(2, JobPriority::Thumbnail, 5000, "/local/2.jpg"));
        QCOMPARE(sched.admittedBytes(), 6000);

        // Release first job
        sched.release(1);
        QCOMPARE(sched.admittedBytes(), 0);

        // Now second job can admit
        QVERIFY(sched.tryAdmit(2, JobPriority::Thumbnail, 5000, "/local/2.jpg"));
        QCOMPARE(sched.admittedBytes(), 5000);
        sched.release(2);
    }

    void testHelperStringConversions() {
        QCOMPARE(storageDeviceTypeToString(StorageDeviceType::Ssd), "SSD");
        QCOMPARE(storageDeviceTypeToString(StorageDeviceType::Rotational), "HDD");
        QCOMPARE(storageDeviceTypeToString(StorageDeviceType::Network), "Network");
        QCOMPARE(storageDeviceTypeToString(StorageDeviceType::Unknown), "Unknown");

        QCOMPARE(storageProfileToString(StorageProfile::Auto), "auto");
        QCOMPARE(storageProfileToString(StorageProfile::Ssd), "ssd");
        QCOMPARE(storageProfileToString(StorageProfile::Rotational), "hdd");
        QCOMPARE(storageProfileToString(StorageProfile::Network), "network");

        QCOMPARE(parseStorageProfile("SSD"), StorageProfile::Ssd);
        QCOMPARE(parseStorageProfile("hdd"), StorageProfile::Rotational);
        QCOMPARE(parseStorageProfile("Rotational"), StorageProfile::Rotational);
        QCOMPARE(parseStorageProfile("network"), StorageProfile::Network);
        QCOMPARE(parseStorageProfile("smb"), StorageProfile::Network);
        QCOMPARE(parseStorageProfile("nfs"), StorageProfile::Network);
        QCOMPARE(parseStorageProfile("invalid"), StorageProfile::Auto);
    }

    void testThumbnailServiceIntegration() {
        auto& sched = IoScheduler::instance();
        sched.setPathOverride("/slow_disk", StorageDeviceType::Rotational);

        ThumbnailService thumbs(GLANCELOT_DECODER_PATH);
        thumbs.setWorkerCount(2);

        // Request a thumbnail on Rotational disk
        const quint64 id1 = thumbs.request("/slow_disk/img1.png", 64, thumbs.epoch());
        QVERIFY(id1 > 0);

        // Admission ledger reflects job or attempt
        thumbs.cancel(id1);
        QCOMPARE(sched.activeForegroundJobs(), 0);

        thumbs.shutdown();
    }

    void testViewerControllerIntegration() {
        ViewerController controller;
        auto& sched = IoScheduler::instance();

        // Check defaults
        QCOMPARE(controller.storageProfile(), "auto");
        QCOMPARE(sched.profile(), StorageProfile::Auto);

        // Setting profile
        controller.setStorageProfile("ssd");
        QCOMPARE(controller.storageProfile(), "ssd");
        QCOMPARE(sched.profile(), StorageProfile::Ssd);

        // Commands
        controller.trigger("storage-profile-hdd");
        QCOMPARE(controller.storageProfile(), "hdd");
        QCOMPARE(sched.profile(), StorageProfile::Rotational);

        controller.trigger("storage-profile-network");
        QCOMPARE(controller.storageProfile(), "network");
        QCOMPARE(sched.profile(), StorageProfile::Network);

        controller.trigger("storage-profile-auto");
        QCOMPARE(controller.storageProfile(), "auto");
        QCOMPARE(sched.profile(), StorageProfile::Auto);

        // Current storage type
        QVERIFY(!controller.currentStorageType().isEmpty());
    }

    void testLookaheadStorageGating() {
        auto& sched = IoScheduler::instance();
        sched.setPathOverride("//remote_share/folder", StorageDeviceType::Network);
        sched.setPathOverride("/local_hdd/folder", StorageDeviceType::Rotational);
        sched.setPathOverride("/local_ssd/folder", StorageDeviceType::Ssd);

        // Network: Lookahead prefetch is completely disallowed to prevent remote congestion
        QVERIFY(!sched.isPrefetchPermitted("//remote_share/folder/item.jpg"));

        // SSD: Lookahead permitted when idle
        QVERIFY(sched.isPrefetchPermitted("/local_ssd/folder/item.jpg"));

        // Rotational: Lookahead permitted when idle, but disallowed when any I/O is active
        QVERIFY(sched.isPrefetchPermitted("/local_hdd/folder/item.jpg"));
        sched.tryAdmit(500, JobPriority::Thumbnail, 2048, "/local_hdd/folder/thumb.jpg");
        QVERIFY(!sched.isPrefetchPermitted("/local_hdd/folder/item.jpg"));
        sched.release(500);
        QVERIFY(sched.isPrefetchPermitted("/local_hdd/folder/item.jpg"));
    }
};

QTEST_MAIN(IoSchedulerTests)
#include "io_scheduler_tests.moc"

