#ifndef TOOLCONTROLCONFIG_H
#define TOOLCONTROLCONFIG_H

#include "openglwidget.h"

#include <QJsonObject>

namespace ToolControlConfig {

QJsonObject loadBuiltInToolLayout(PaintOpenGLWidget::Tool tool,
                                  int smoothValue,
                                  int penWidth,
                                  PaintOpenGLWidget::FillScope fillScope);

}

#endif // TOOLCONTROLCONFIG_H
