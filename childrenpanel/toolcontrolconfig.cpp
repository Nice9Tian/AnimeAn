#include "toolcontrolconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace {
QString fileNameForTool(PaintOpenGLWidget::Tool tool)
{
    switch (tool) {
    case PaintOpenGLWidget::Tool::Pen:
        return QStringLiteral("pen.json");
    case PaintOpenGLWidget::Tool::Fill:
        return QStringLiteral("fill.json");
    case PaintOpenGLWidget::Tool::Eraser:
    case PaintOpenGLWidget::Tool::DeleteLine:
    case PaintOpenGLWidget::Tool::CutLine:
        return QStringLiteral("eraser.json");
    case PaintOpenGLWidget::Tool::Arrow:
        return QStringLiteral("arrow.json");
    case PaintOpenGLWidget::Tool::Connect:
        return QStringLiteral("connect.json");
    case PaintOpenGLWidget::Tool::Transfer:
        return QStringLiteral("transfer.json");
    }
    return QStringLiteral("pen.json");
}

QString resolveToolControlPath(const QString &fileName)
{
    const QString appPath = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appPath).filePath(QStringLiteral("childrenpanel/tool_controls/%1").arg(fileName)),
        QDir(appPath).filePath(QStringLiteral("tool_controls/%1").arg(fileName)),
#ifdef ANIMEAN_SOURCE_DIR
        QDir(QStringLiteral(ANIMEAN_SOURCE_DIR)).filePath(QStringLiteral("childrenpanel/tool_controls/%1").arg(fileName)),
#endif
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

void setControlValue(QJsonObject *control, const QString &name, const QJsonValue &value)
{
    if (control->value(QStringLiteral("name")).toString() == name) {
        control->insert(QStringLiteral("value"), value);
    }
}

QJsonObject applyRuntimeState(QJsonObject layout,
                             PaintOpenGLWidget::Tool tool,
                             int smoothValue,
                             int penWidth,
                             PaintOpenGLWidget::FillScope fillScope)
{
    const QString fillScopeValue = fillScope == PaintOpenGLWidget::FillScope::AllLayers
                                       ? QStringLiteral("all")
                                       : QStringLiteral("current");
    const QString eraserModeValue =
        tool == PaintOpenGLWidget::Tool::DeleteLine ? QStringLiteral("line")
        : tool == PaintOpenGLWidget::Tool::CutLine ? QStringLiteral("cut")
                                                   : QStringLiteral("area");

    QJsonArray controls = layout.value(QStringLiteral("controls")).toArray();
    for (int index = 0; index < controls.size(); ++index) {
        if (!controls.at(index).isObject()) {
            continue;
        }

        QJsonObject control = controls.at(index).toObject();
        setControlValue(&control, QStringLiteral("smooth"), smoothValue);
        setControlValue(&control, QStringLiteral("pen_width"), penWidth);
        setControlValue(&control, QStringLiteral("fill_scope"), fillScopeValue);
        setControlValue(&control, QStringLiteral("eraser_mode"), eraserModeValue);
        controls.replace(index, control);
    }

    layout.insert(QStringLiteral("controls"), controls);
    return layout;
}
}

namespace ToolControlConfig {

QJsonObject loadBuiltInToolLayout(PaintOpenGLWidget::Tool tool,
                                  int smoothValue,
                                  int penWidth,
                                  PaintOpenGLWidget::FillScope fillScope)
{
    const QString path = resolveToolControlPath(fileNameForTool(tool));
    if (path.isEmpty()) {
        return QJsonObject();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QJsonObject();
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonObject();
    }

    return applyRuntimeState(document.object(), tool, smoothValue, penWidth, fillScope);
}

}
