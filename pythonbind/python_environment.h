#pragma once

// Points the embedded Python interpreter at its runtime and at AnimeAn's
// scripts. One home for it: the backend and the headless board test both start
// an interpreter and must agree on where things are found.

enum class PythonRuntimeChoice {
    // The runtime folder beside the program when it is there (a release folder
    // carries its own), otherwise the one the build was configured with.
    PreferBundled,
    // The build's own runtime first: for programs run from the build tree.
    PreferConfigured,
};

// Sets PYTHONHOME, PYTHONPATH and the runtime's entries at the front of PATH.
// Call it before the interpreter starts.
//
// On sys.path the source folders (pyfile, pythonbind, opentoonz_tools) come
// before the program's own folder when they exist: on a development machine an
// edited script wins over the copy beside the program. A release folder has no
// source tree and imports its own copies.
void preparePythonEnvironment(PythonRuntimeChoice choice);
