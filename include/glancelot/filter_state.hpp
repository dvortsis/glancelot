#pragma once
#include <QString>

namespace glancelot {

enum class FilterPersistence {
    Temporary,
    Persistent
};

struct FilterSpec {
    QString pattern;
    FilterPersistence persistence{FilterPersistence::Temporary};
    bool caseSensitive{false};

    bool isActive() const {
        return !pattern.trimmed().isEmpty();
    }

    bool isPersistent() const {
        return persistence == FilterPersistence::Persistent;
    }

    bool matches(const QString& text) const {
        if (!isActive()) return true;
        const auto cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        return text.contains(pattern.trimmed(), cs);
    }

    void clear() {
        pattern.clear();
    }
};

} // namespace glancelot
