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
    // The bundled format must NOT reuse the v1 marker: pre-bundle builds
    // validate only the format string and would open a bundle as a blank
    // scene instead of rejecting it.
    check(project.value(QStringLiteral("format")).toString() == QStringLiteral("AnimeAn Project 2"),
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

    // Pre-release: the v1 flat .animean format has no reader any more.
    QJsonObject flatV1;
    flatV1[QStringLiteral("format")] = QStringLiteral("AnimeAn Project");
    flatV1[QStringLiteral("version")] = 1;
    flatV1[QStringLiteral("xsheet")] = QJsonObject();
    error.clear();
    check(!projectFromJson(flatV1, &loadedMain, &loadedTexture, &error),
          "a v1 flat file was accepted as a project");
    error.clear();
    check(!textureViewFromJson(flatV1, &loadedTextureView, &error),
          "a v1 flat file was accepted as a texture view");

    // A damaged file that kept its envelope but lost the scene data must be
    // rejected: loading it as a blank scene and reporting success would
    // invite a save that overwrites the original. This holds for a
    // standalone texture view AND for each payload embedded in a bundle.
    QJsonObject damagedTexture;
    damagedTexture[QStringLiteral("format")] = QStringLiteral("AnimeAn Texture View");
    damagedTexture[QStringLiteral("version")] = 1;
    error.clear();
    check(!textureViewFromJson(damagedTexture, &loadedTextureView, &error),
          "a data-less texture envelope was accepted");

    QJsonObject damagedBundle = project;
    damagedBundle[QStringLiteral("textureView")] = QJsonObject();
    error.clear();
    check(!projectFromJson(damagedBundle, &loadedMain, &loadedTexture, &error),
          "a bundle with a data-less view payload was accepted");

    QJsonObject wrongVersion = project;
    wrongVersion[QStringLiteral("version")] = QStringLiteral("2");
    error.clear();
    check(!projectFromJson(wrongVersion, &loadedMain, &loadedTexture, &error),
          "a bundle with a non-numeric version was accepted");

    check(ensureProjectFileExtension(QStringLiteral("drawing")) == QStringLiteral("drawing.anproj"),
          "project extension was not appended");
    check(ensureProjectFileExtension(QStringLiteral("cloth.textureview"))
              == QStringLiteral("cloth.anproj"),
          "cross-type extension was not replaced");
    check(ensureTextureViewFileExtension(QStringLiteral("cloth.TEXTUREVIEW"))
              == QStringLiteral("cloth.TEXTUREVIEW"),
          "existing texture extension was changed");

    if (failures == 0) {
        qInfo() << "projectio round-trip tests passed";
    }
    return failures == 0 ? 0 : 1;
}
