#include "scenehistory.h"

namespace {
quint64 nextHistorySequence()
{
    static quint64 sequence = 0;
    return ++sequence;
}
}

void SceneHistory::reset(const QString &label, const AnimeSceneModel &model)
{
    m_entries.clear();
    Entry entry;
    entry.label = label;
    entry.state = model;
    m_entries.append(entry);
    m_current = 0;
}

void SceneHistory::commit(const QString &label, const AnimeSceneModel &model)
{
    if (m_current < m_entries.size() - 1) {
        m_entries.resize(m_current + 1);
    }

    Entry entry;
    entry.label = label;
    entry.seq = nextHistorySequence();
    entry.state = model;
    m_entries.append(entry);

    if (m_entries.size() > m_maxEntries) {
        m_entries.remove(0, m_entries.size() - m_maxEntries);
    }
    m_current = m_entries.size() - 1;
}

bool SceneHistory::undo(AnimeSceneModel *model)
{
    return goTo(m_current - 1, model);
}

bool SceneHistory::redo(AnimeSceneModel *model)
{
    return goTo(m_current + 1, model);
}

bool SceneHistory::goTo(int index, AnimeSceneModel *model)
{
    if (!model || index < 0 || index >= m_entries.size() || index == m_current) {
        return false;
    }
    *model = m_entries[index].state;
    m_current = index;
    return true;
}

bool SceneHistory::truncateRedo()
{
    if (!canRedo()) {
        return false;
    }
    m_entries.resize(m_current + 1);
    return true;
}

bool SceneHistory::canUndo() const
{
    return m_current > 0;
}

bool SceneHistory::canRedo() const
{
    return m_current >= 0 && m_current < m_entries.size() - 1;
}

int SceneHistory::count() const
{
    return m_entries.size();
}

int SceneHistory::currentIndex() const
{
    return m_current;
}

QString SceneHistory::labelAt(int index) const
{
    if (index < 0 || index >= m_entries.size()) {
        return QString();
    }
    return m_entries[index].label;
}

quint64 SceneHistory::currentSeq() const
{
    if (m_current < 0 || m_current >= m_entries.size()) {
        return 0;
    }
    return m_entries[m_current].seq;
}

quint64 SceneHistory::redoSeq() const
{
    if (!canRedo()) {
        return 0;
    }
    return m_entries[m_current + 1].seq;
}
