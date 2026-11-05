#pragma once
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

// Lead-frozen CAPABILITY (docs/contracts/CAPABILITY.md). Implemented by packet D2. One place that states what this build, this
// machine and this source can do, with unknown as an explicit answer. Display only: services recheck at operation time.
namespace glancelot {
enum class CapabilitySection : quint8 { Decoder, Colour, Display, Source };
// Unknown is the default and the honest answer when nothing has established the fact.
enum class Support : quint8 { Unknown, Unsupported, Partial, Supported };
// How the value was established. Runtime code never claims physical qualification: that is RUNBOOK evidence only.
enum class Evidence : quint8 { None, BuildFact, RuntimeProbe, Assumed, UserSetting };

struct CapabilityItem {
    QString id;            // stable dotted id, e.g. "decoder.webp.still", "display.<displayId>.icc", "source.readOnly"
    CapabilitySection section{CapabilitySection::Decoder};
    Support support{Support::Unknown};
    Evidence evidence{Evidence::None};
    QString label;         // translated short name
    QString detail;        // translated user-facing value or reason ("Read-only mount")
    QString value;         // untranslated machine value ("D3D11", "sRGB", "6.8.3"); never a host path guess
};
// One section replaced atomically. `scope` fences per-context sections: Source = directory key; Display and Colour = the
// window's screen id ("" = process-wide). `revision` is the populator's monotonic counter for that section and scope.
struct CapabilitySnapshot {
    CapabilitySection section{CapabilitySection::Decoder};
    QString scope;
    quint64 revision{};
    QList<CapabilityItem> items;
};

// STACK-CONFIRM G-5: whether the F040 reference window can be shown, and placed, in this session.
inline constexpr char kReferenceWindowCapabilityId[] = "display.referenceWindow";
struct SecondaryWindowFacts {
    bool multipleWindows{};      // QPlatformIntegration::hasCapability(MultipleWindows)
    bool canPosition{};          // not Wayland: the same rule as DisplayManager::displaysVariant "canPosition"
    bool containerDeployment{};  // GLANCELOT_DEPLOYMENT=container, the single-output remote desktop
};
// Unsupported: the pin is refused with a message. Partial: it opens, and the detail says it cannot be placed. Supported never
// means physically qualified; its evidence is RuntimeProbe.
constexpr Support referenceWindowSupport(const SecondaryWindowFacts& facts) {
    if (!facts.multipleWindows) return Support::Unsupported;
    if (facts.containerDeployment || !facts.canPosition) return Support::Partial;
    return Support::Supported;
}
inline constexpr int kReferenceWindowExposeDeadlineMs = 2000; // not exposed by then: unsupported state, see the contract

// GUI thread only. Populators build snapshots on worker threads and deliver them by queued call to apply().
// Bounds: at most kMaxItems rows in total and kMaxItemsPerSection per snapshot (extra items are dropped and an
// "<section>.truncated" item is added), kMaxDisplays display scopes retained.
class CapabilityModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString decoderSummary READ decoderSummary NOTIFY sectionChanged)
    Q_PROPERTY(QString colourSummary READ colourSummary NOTIFY sectionChanged)
    Q_PROPERTY(QString displaySummary READ displaySummary NOTIFY sectionChanged)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY sectionChanged)
    // Display-only conveniences for F075 and Track B. Never authorization: services recheck at operation time.
    // serverPaths is a process fact set once at startup, so it does not drop when the Source section clears.
    Q_PROPERTY(QString sourceReadOnly READ sourceReadOnly NOTIFY sectionChanged)   // "unknown" | "yes" | "no"
    Q_PROPERTY(bool serverPaths READ serverPaths NOTIFY sectionChanged)
    // "unknown" | "unsupported" | "partial" | "supported"; read by the reference-pin command and the reference window.
    Q_PROPERTY(QString referenceWindow READ referenceWindow NOTIFY sectionChanged)
public:
    static constexpr int kMaxItems = 256, kMaxItemsPerSection = 96, kMaxDisplays = 16;
    enum Role { IdRole = Qt::UserRole + 1, SectionRole, SupportRole, EvidenceRole, LabelRole, DetailRole, ValueRole };
    explicit CapabilityModel(QObject* parent = nullptr);
    ~CapabilityModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Returns false (no change) when the scope is not current or the revision is not newer than the stored one.
    bool apply(CapabilitySnapshot snapshot);
    // Current contexts. Changing the source scope clears the Source section to Unknown until a matching snapshot arrives.
    void setSourceScope(const QString& directoryKey);
    void setScreenScope(const QString& displayId);

    Q_INVOKABLE QString support(const QString& id) const;   // "unknown" | "unsupported" | "partial" | "supported"; absent id -> "unknown"
    Q_INVOKABLE QVariantMap item(const QString& id) const;  // {} for an absent id
    Q_INVOKABLE QVariantList section(const QString& name) const; // "decoder" | "colour" | "display" | "source"
    Q_INVOKABLE QString exportText() const;                 // for E2; source paths replaced by "<source>"
    QString decoderSummary() const; QString colourSummary() const; QString displaySummary() const;
    QString sourceSummary() const; QString sourceReadOnly() const; bool serverPaths() const;
    QString referenceWindow() const;
signals:
    void sectionChanged(glancelot::CapabilitySection section);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
