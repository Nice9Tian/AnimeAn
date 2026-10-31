#ifndef UIDUMP_H
#define UIDUMP_H

class QWidget;

// Baseline for the web front end: when ANIMEAN_UI_DUMP names a folder, write
// the opened window's pixels and widget tree there. Does nothing otherwise.
void scheduleUiDump(QWidget *window);

#endif // UIDUMP_H
