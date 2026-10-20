#pragma once
#include <QJsonObject>
#include <QKeySequence>
#include <QList>

namespace glancelot::bindings {
inline bool usableShortcut(const QKeySequence& sequence) {
    for (int i=0; i<sequence.count(); ++i) {
        const auto key=sequence[i];
        if (key.key()==Qt::Key_unknown || key==QKeyCombination(Qt::NoModifier,Qt::Key_Escape)) return false;
    }
    return true;
}
inline bool shortcutOverlap(const QKeySequence& left,const QKeySequence& right) {
    return !left.isEmpty() && !right.isEmpty()
        && (left.matches(right)!=QKeySequence::NoMatch || right.matches(left)!=QKeySequence::NoMatch);
}
inline bool usableBindings(const QJsonObject& bindings) {
    QList<QKeySequence> seen;
    for (const auto& value:bindings) {
        const auto text=value.toString();
        const auto sequence=QKeySequence::fromString(text,QKeySequence::PortableText);
        if ((!text.trimmed().isEmpty() && sequence.isEmpty()) || !usableShortcut(sequence)) return false;
        for (const auto& other:seen) if (shortcutOverlap(sequence,other)) return false;
        seen.append(sequence);
    }
    return true;
}
}
