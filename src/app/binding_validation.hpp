#pragma once
#include "glancelot/command_registry.hpp"
#include <QKeySequence>
#include <QList>
#include <utility>

namespace glancelot::bindings {
inline bool usableShortcut(const QKeySequence& sequence) {
    if (sequence.count()>2) return false;
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
inline const Command* findCommand(const QString& id) {
    for (const auto& command:glancelot::commandRegistry()) if (QString::fromStdString(command.id)==id) return &command;
    return nullptr;
}
inline QString scopeOf(const QString& id) {
    const auto* command=findCommand(id);
    return command?QString::fromStdString(command->scope):QStringLiteral("global");
}
// "global" overlaps every scope and "workspace" overlaps the two content panes; other scopes only themselves.
inline bool scopesOverlap(const QString& left,const QString& right) {
    if (left==right || left=="global" || right=="global") return true;
    auto content=[](const QString& scope){ return scope=="canvas" || scope=="grid"; };
    return (left=="workspace" && content(right)) || (right=="workspace" && content(left));
}
inline bool bindingsCollide(const QString& leftId,const QKeySequence& left,const QString& rightId,const QKeySequence& right) {
    return scopesOverlap(scopeOf(leftId),scopeOf(rightId)) && shortcutOverlap(left,right);
}
inline bool usableBindings(const QJsonObject& bindings) {
    QList<std::pair<QString,QKeySequence>> seen;
    for (auto it=bindings.begin();it!=bindings.end();++it) {
        const auto text=it.value().toString();
        const auto sequence=QKeySequence::fromString(text,QKeySequence::PortableText);
        if ((!text.trimmed().isEmpty() && sequence.isEmpty()) || !usableShortcut(sequence)) return false;
        const auto* command=findCommand(it.key());
        if (command && command->hold && sequence.count()>1) return false; // a hold needs one key to release
        for (const auto& other:seen) if (bindingsCollide(it.key(),sequence,other.first,other.second)) return false;
        seen.append({it.key(),sequence});
    }
    return true;
}
}
