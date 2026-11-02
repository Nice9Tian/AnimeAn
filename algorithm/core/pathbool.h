#ifndef ANIMECORE_PATHBOOL_H
#define ANIMECORE_PATHBOOL_H

#include "algorithm/core/path.h"

namespace AnimeCore {

// Boolean operations on whole paths, the counterparts of Path's members.
// Curves are flattened on the way in, so every result is made of straight
// closed subpaths, which is also what Qt returns.
Path simplified(const Path &path);
Path united(const Path &a, const Path &b);
Path subtracted(const Path &a, const Path &b);
Path intersected(const Path &a, const Path &b);

// `outer` holds `inner` strictly: outlines that touch do not count, which is
// how the fill tells a hole from a neighbouring face that shares a wall.
bool contains(const Path &outer, const Path &inner);
bool intersects(const Path &a, const Path &b);

// The outline of a round-capped, round-joined stroke of total width `width`.
Path strokeOutline(const Path &path, double width);

} // namespace AnimeCore

#endif // ANIMECORE_PATHBOOL_H
