#ifndef BOARDBASE_H
#define BOARDBASE_H

#ifdef ANIMEAN_HEADLESS
#include "backend/board/boardsurface.h"
using PaintBoardBase = BoardSurface;
#else
#include <QOpenGLWidget>
using PaintBoardBase = QOpenGLWidget;
#endif

#endif // BOARDBASE_H
