#include "projectio.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>

namespace {
int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical().noquote() << message;
        ++failures;
    }
}

void checkModel(const AnimeSceneModel &model,
                const QString &scriptData,
                const QSize &canvasSize,
                int frameCount,
                const QString &firstLayerName)
{
    check(model.scriptData() == scriptData, "script data did not round-trip");
    check(model.canvasSize() == canvasSize, "canvas size did not round-trip");
    check(model.frameCount() == frameCount, "frame count did not round-trip");
    check(model.layerName(0) == firstLayerName, "layer name did not round-trip");
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    AnimeSceneModel mainModel;
    mainModel.initializeScene(3, 5);
    mainModel.setTextId(QStringLiteral("main_paint_view"));
    mainModel.setIntId(1);
    mainModel.setCanvasSize(QSize(1920, 1080));
    mainModel.setLayerName(0, QStringLiteral("Main ink"));
    mainModel.setScriptData(QStringLiteral("{\"owner\":\"main\"}"));

    AnimeSceneModel textureModel;
    textureModel.initializeScene(2, 7);
    textureModel.setTextId(QStringLiteral("child_paint_view"));
    textureModel.setIntId(2);
    textureModel.setCanvasSize(QSize(512, 512));
    textureModel.setLayerName(0, QStringLiteral("Texture ink"));
    textureModel.setScriptData(QStringLiteral("{\"owner\":\"texture\"}"));

    const QJsonObject project = projectToJson(mainModel, textureModel);
    check(project.value(QStringLiteral("format")).toString() == QStringLiteral("AnimeAn Project"),
          "project format marker is wrong");
    check(project.value(QStringLiteral("version")).toInt() == 2,
          "project format version is wrong");
    check(project.value(QStringLiteral("mainView")).isObject(),
          "project is missing the main view");
    check(project.value(QStringLiteral("textureView")).isObject(),
          "project is missing the texture view");

    AnimeSceneModel loadedMain;
    AnimeSceneModel loadedTexture;
    QString error;
    check(projectFromJson(project, &loadedMain, &loadedTexture, &error),
          qPrintable(QStringLiteral("project round-trip failed: %1").arg(error)));
    checkModel(loadedMain,
               QStringLiteral("{\"owner\":\"main\"}"),
               QSize(1920, 1080),
               5,
               QStringLiteral("Main ink"));
    checkModel(loadedTexture,
               QStringLiteral("{\"owner\":\"texture\"}"),
               QSize(512, 512),
               7,
               QStringLiteral("Texture ink"));

    const QJsonObject textureView = textureViewToJson(textureModel);
    check(textureView.value(QStringLiteral("format")).toString()
              == QStringLiteral("AnimeAn Texture View"),
          "texture view format marker is wrong");
    AnimeSceneModel loadedTextureView;
    error.clear();
    check(textureViewFromJson(textureView, &loadedTextureView, &error),
          qPrintable(QStringLiteral("texture view round-trip failed: %1").arg(error)));
    checkModel(loadedTextureView,
               QStringLiteral("{\"owner\":\"texture\"}"),
               QSize(512, 512),
               7,
               QStringLiteral("Texture ink"));

    error.clear();
    check(!projectFromJson(textureView, &loadedMain, &loadedTexture, &error),
          "a texture view was accepted as a complete project");
    error.clear();
    check(!textureViewFromJson(project, &loadedTextureView, &error),
          "a complete project was accepted as a texture view");

    const QJsonObject legacy = modelToJson(mainModel);
    error.clear();
    check(projectFromJson(legacy, &loadedMain, &loadedTexture, &error),
          qPrintable(QStringLiteral("legacy project import failed: %1").arg(error)));
    checkModel(loadedMain,
               QStringLiteral("{\"owner\":\"main\"}"),
               QSize(1920, 1080),
               5,
               QStringLiteral("Main ink"));
    error.clear();
    check(textureViewFromJson(legacy, &loadedTextureView, &error),
          qPrintable(QStringLiteral("legacy texture import failed: %1").arg(error)));

    check(ensureProjectFileExtension(QStringLiteral("drawing")) == QStringLiteral("drawing.anproj"),
          "project extension was not appended");
    check(ensureProjectFileExtension(QStringLiteral("drawing.animean")) == QStringLiteral("drawing.anproj"),
          "legacy project extension was not migrated");
    check(ensureTextureViewFileExtension(QStringLiteral("cloth.animean"))
              == QStringLiteral("cloth.textureview"),
          "legacy texture extension was not migrated");
    check(ensureTextureViewFileExtension(QStringLiteral("cloth.TEXTUREVIEW"))
              == QStringLiteral("cloth.TEXTUREVIEW"),
          "existing texture extension was changed");

    if (failures == 0) {
        qInfo() << "projectio round-trip tests passed";
    }
    return failures == 0 ? 0 : 1;
}
