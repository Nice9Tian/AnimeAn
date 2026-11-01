#ifndef SCENEHISTORY_H
#define SCENEHISTORY_H

#include "animemodel.h"

#include "algorithm/core/containers.h"
#include "algorithm/core/string.h"

#include <cstdint>


// Snapshot-based undo/redo for one AnimeSceneModel.
//
// Every entry stores a full copy of the model taken right AFTER an operation
// completed; entry 0 is the baseline state. Because AnimeSceneModel is built
// entirely from implicitly-shared core containers, a snapshot is a cheap
// shallow copy — memory is only spent on the parts a later operation
// actually mutates.
class SceneHistory
{
public:
    struct Entry {
        AnimeCore::String label;
        std::uint64_t seq = 0;
        AnimeSceneModel state;
    };

    // Drops everything and starts over with `model` as the baseline entry.
    void reset(const AnimeCore::String &label, const AnimeSceneModel &model);

    // Records the state after an operation. Discards any redo tail first.
    void commit(const AnimeCore::String &label, const AnimeSceneModel &model);

    bool undo(AnimeSceneModel *model);
    bool redo(AnimeSceneModel *model);
    bool goTo(int index, AnimeSceneModel *model);

    // Discards the redo tail without adding an entry. Returns true if
    // anything was dropped.
    bool truncateRedo();

    bool canUndo() const;
    bool canRedo() const;
    int count() const;
    int currentIndex() const;
    AnimeCore::String labelAt(int index) const;

    // Global operation ordering across independent histories: every commit is
    // stamped from one shared counter, so callers holding several histories
    // can undo/redo in true chronological order. Baselines carry seq 0.
    std::uint64_t currentSeq() const;
    std::uint64_t redoSeq() const;

private:
    AnimeCore::Vector<Entry> m_entries;
    int m_current = -1;
    int m_maxEntries = 100;
};

#endif // SCENEHISTORY_H
