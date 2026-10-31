// Source: Copied from C:/Users/admin/Documents/VibeFrontend/animean/business/animeanbusiness.h
#ifndef ANIMEANBUSINESS_H
#define ANIMEANBUSINESS_H

#include "algorithm/animemodel.h"
#include "algorithm/scenehistory.h"
#include "algorithm/vectorlogic.h"
#include "selectionattention.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QPointF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <functional>

class QTimer;

struct AnimeLayerPanelRow {
    bool group = false;
    int layerIndex = -1;
    int groupId = 0;
    QString name;
    bool visible = true;
    bool collapsed = false;
    int depth = 0;
    bool child = false;
    bool parent = false;
};

struct AnimeExtraToolDefinition {
    QString name;
    QString title;
    QString property;
    QString handler;
    QString baseTool;
    QString page = QStringLiteral("mapping");
};

struct AnimeTimelineState {
    QString viewName;
    int frameCount = 0;
    int currentFrame = 0;
    QVector<bool> holds;
    QVector<QString> frameNames;
    int fps = 12;
    bool playbackActive = false;
    bool loop = true;
    bool onionAvailable = true;
    bool onionEnabled = false;
    bool onionLines = true;
    bool onionFills = true;
    bool onionAmLayers = true;
    bool onionGuideLines = false;
    QSet<int> onionFrames;
};

struct AnimePaintViewAdapter {
    QObject *viewObject = nullptr;
    AnimeSceneModel *model = nullptr;
    SceneHistory *history = nullptr;

    std::function<void()> modelReplaced;
    std::function<void(const QSize &)> setCanvasSize;
    std::function<void()> requestUpdate;
    std::function<void(int)> setCurrentFrame;
    std::function<void(int)> setCurrentLayer;
    std::function<void(int)> setCurrentAsset;
    std::function<void()> notifyLayerChanged;
    std::function<void()> removeInvalidFillRegions;

    std::function<void(const QString &)> commitHistory;
    std::function<void(const QString &)> resetHistory;
    std::function<void()> dropRedoTail;
    std::function<bool()> undoHistory;
    std::function<bool()> redoHistory;
    std::function<bool(int)> goToHistory;

    std::function<int()> addLayer;
    std::function<bool(int)> deleteLayer;
    std::function<int(int)> deleteLayerGroup;
    std::function<bool(int, int)> moveLayer;
    std::function<bool(int, bool)> sendLayerVisibilityMessage;

    std::function<int()> addFrame;
    std::function<int(int)> insertHoldFrameAfter;
    std::function<int(int)> duplicateFrame;
    std::function<bool(int)> deleteFrame;
    std::function<bool(int, int)> moveFrame;

    std::function<int(AnimeColumnType, const QString &)> addAsset;
    std::function<bool(int, int)> assignAssetToLayer;
    std::function<int(int)> addLayerForAsset;

    std::function<void(int)> setTool;
    std::function<int()> currentTool;
    std::function<void(const QString &)> setStrokeProperty;
    std::function<void(const QColor &)> setDrawingColor;
    std::function<void(qreal)> setPenWidth;
    std::function<void(int)> setFillScope;
    std::function<void(int)> setSmoothValue;
    std::function<void(const AnimeStrokeFitSettings &)> setStrokeFitSettings;
    std::function<void(const QString &, const QString &)> sendExtraToolMessage;
    std::function<void(const QString &, const QString &, const QString &, const QVariant &, int, int, int)> sendToolOptionMessage;
    std::function<void(const QString &, const QString &, double, double)> sendPadMessage;

    std::function<bool(int, QString *)> buildPlaybackCache;
    std::function<void(int)> showPlaybackFrame;
    std::function<void()> endPlayback;

    std::function<void(bool)> setOnionEnabled;
    std::function<void(const QSet<int> &)> setOnionFrames;
    std::function<void(bool)> setOnionLines;
    std::function<void(bool)> setOnionFills;
    std::function<void(bool)> setOnionAmLayers;
    std::function<void(bool)> setOnionGuideLines;
    std::function<void(const QStringList &)> setOnionExcludeProperties;
    std::function<void(const QString &)> setOnionLayerTag;

    std::function<QImage(int, const QSize &)> renderFrameThumbnail;
};

class AnimeanBusiness : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString statusMessage READ statusMessage WRITE setStatusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString currentFilePath READ currentFilePath NOTIFY projectFilePathChanged)
    Q_PROPERTY(QString childFilePath READ childFilePath NOTIFY childFilePathChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(QSize canvasSize READ canvasSize NOTIFY canvasSizeChanged)
    Q_PROPERTY(QString activeViewName READ activeViewName WRITE setActiveViewName NOTIFY activeViewChanged)
    Q_PROPERTY(QObject *mainPaintViewObject READ mainPaintViewObject NOTIFY mainPaintViewObjectChanged)
    Q_PROPERTY(QObject *childPaintViewObject READ childPaintViewObject NOTIFY childPaintViewObjectChanged)
    Q_PROPERTY(QObject *activePaintViewObject READ activePaintViewObject NOTIFY activePaintViewObjectChanged)
    Q_PROPERTY(int currentTool READ currentTool NOTIFY toolChanged)
    Q_PROPERTY(QString activeExtraTool READ activeExtraTool NOTIFY toolChanged)
    Q_PROPERTY(int toolSmoothValue READ toolSmoothValue WRITE setSmoothValue NOTIFY toolOptionsStateChanged)
    Q_PROPERTY(int toolPenWidth READ toolPenWidth WRITE setPenWidth NOTIFY toolOptionsStateChanged)
    Q_PROPERTY(bool toolFillAllLayers READ toolFillAllLayers WRITE setFillAllLayers NOTIFY toolOptionsStateChanged)
    Q_PROPERTY(QColor drawingColor READ drawingColor WRITE setDrawingColor NOTIFY drawingColorChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY timelineChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY timelineChanged)
    Q_PROPERTY(int playbackFps READ playbackFps WRITE setFps NOTIFY timelineChanged)
    Q_PROPERTY(bool playbackActive READ isPlaybackActive NOTIFY playbackStateChanged)
    Q_PROPERTY(bool playbackLoop READ isPlaybackLoop WRITE setPlaybackLoop NOTIFY timelineChanged)
    Q_PROPERTY(bool onionEnabled READ isOnionEnabled WRITE setOnionEnabled NOTIFY onionStateChanged)

public:
    enum class Tool {
        Pen = 0,
        Eraser = 1,
        DeleteLine = 2,
        CutLine = 3,
        Fill = 4,
        Arrow = 5,
        Connect = 6,
        Transfer = 7
    };
    Q_ENUM(Tool)

    enum class FillScope {
        CurrentLayer = 0,
        AllLayers = 1
    };
    Q_ENUM(FillScope)

    explicit AnimeanBusiness(QObject *parent = nullptr);
    ~AnimeanBusiness() override;

    // --- 绘图区提供方式 (Paint View Provision & Binding) ---
    void bindPaintView(const QString &viewName, const AnimePaintViewAdapter &adapter);
    void unbindPaintView(const QString &viewName);
    QObject *paintViewObject(const QString &viewName) const;
    QObject *mainPaintViewObject() const;
    QObject *childPaintViewObject() const;
    QObject *activePaintViewObject() const;

    QString activeViewName() const;
    QString framePanelTargetViewName() const;
    QString layerPanelTargetViewName() const;
    QString assetPanelTargetViewName() const;
    bool changeableTimeline() const;
    bool changeableTexture() const;

    AnimeSceneModel &modelForView(const QString &viewName = QStringLiteral("main"));
    const AnimeSceneModel &modelForView(const QString &viewName = QStringLiteral("main")) const;
    SceneHistory &historyForView(const QString &viewName = QStringLiteral("main"));
    const SceneHistory &historyForView(const QString &viewName = QStringLiteral("main")) const;

    SelectionAttention &attentionForView(const QString &viewName);
    SelectionAttention attentionForView(const QString &viewName) const;
    void syncAllViewAttentionsFromModels();

    QImage renderFrameThumbnail(int frameIndex,
                                const QSize &size,
                                const QString &viewName = QString());

    // --- 状态栏消息 (Status Bar Message) ---
    QString statusMessage() const;

    // --- 项目新建打开保存 (Project New / Open / Save) ---
    QString currentFilePath() const;
    void setCurrentFilePath(const QString &filePath);
    QString childFilePath() const;
    void setChildFilePath(const QString &filePath);
    QString windowTitle() const;
    QSize canvasSize(const QString &viewName = QStringLiteral("main")) const;

    static QString ensureProjectFileExtension(const QString &fileName);
    static QString ensureTextureViewFileExtension(const QString &fileName);
    static QString projectOpenFilter();
    static QString projectSaveFilter();
    static QString textureViewOpenFilter();
    static QString textureViewSaveFilter();

    void installLoadedModel(const QString &viewName, const AnimeSceneModel &model);
    void activateLoadedModel(const QString &viewName, const QString &historyLabel);
    void adoptLoadedModel(const QString &viewName,
                          const AnimeSceneModel &model,
                          const QString &historyLabel);

    // --- 图层增删移动显示隐藏 (Layers) ---
    int layerCount(const QString &viewName = QStringLiteral("main")) const;
    int currentLayer(const QString &viewName = QStringLiteral("main")) const;
    QString layerName(int layerIndex, const QString &viewName = QStringLiteral("main")) const;
    bool isLayerVisible(int layerIndex, const QString &viewName = QStringLiteral("main")) const;
    QVector<AnimeLayerPanelRow> layerRowsForView(const QString &viewName = QStringLiteral("main")) const;
    int reorderLayerTree(const QString &viewName,
                         const QVector<AnimeLayerNode> &capturedTree,
                         int movedColumnId,
                         const QVector<QPair<int, int>> &flatGroupAndLayerRoles);

    // --- 当前工具和工具选项（含 toolcontrol.options_for_tool_json） (Tools & Tool Options) ---
    static QString toolControlName(Tool tool);
    static QString toolLockName(Tool tool);
    static QVector<Tool> lockableTools();

    int currentTool() const;
    Tool currentToolEnum() const;
    QString activeExtraTool() const;
    QString activeExtraToolProperty() const;
    int toolSmoothValue() const;
    int toolPenWidth() const;
    bool toolFillAllLayers() const;
    FillScope toolFillScope() const;
    QColor drawingColor() const;
    AnimeStrokeFitSettings strokeFitSettings() const;

    QVector<AnimeExtraToolDefinition> loadExtraTools();
    QVector<AnimeExtraToolDefinition> extraTools() const;
    static QVector<AnimeExtraToolDefinition> parseExtraToolsJson(const QJsonArray &array);

    QString optionsForToolJson(Tool tool) const;
    QString optionsForToolJsonByName(const QString &toolName) const;
    QJsonObject toolOptionsLayout(Tool tool) const;
    QString optionsForExtraToolJson(const QString &extraToolName) const;
    QJsonObject extraToolOptionsLayout(const QString &extraToolName) const;
    static QJsonObject loadBuiltInToolLayout(Tool tool,
                                             int smoothValue,
                                             int penWidth,
                                             FillScope fillScope);

    bool isToolLocked(int tool) const;
    QSet<QString> lockedToolsForView(const QString &viewName) const;

    // --- 素材 (Assets) ---
    int assetCount(const QString &viewName = QString()) const;
    int currentAsset(const QString &viewName = QString()) const;
    QString assetName(int assetIndex, const QString &viewName = QString()) const;
    QStringList visibleAssetNames(const QString &viewName = QString()) const;

    // --- 历史 (History) ---
    QString undoTargetViewName() const;
    QString redoTargetViewName() const;
    bool canUndo() const;
    bool canRedo() const;
    QStringList historyLabels(const QString &viewName = QString()) const;
    int currentHistoryIndex(const QString &viewName = QString()) const;

    // --- 时间轴和播放 (Timeline, Playback & Onion Skin) ---
    int frameCount(const QString &viewName = QString()) const;
    int currentFrame(const QString &viewName = QString()) const;
    int playbackFps(const QString &viewName = QString()) const;
    bool isPlaybackActive() const;
    int playbackIndex() const;
    QString playbackViewName() const;
    bool isPlaybackLoop() const;

    bool isOnionEnabled() const;
    bool isOnionLines() const;
    bool isOnionFills() const;
    bool isOnionAmLayers() const;
    bool isOnionGuideLines() const;
    QSet<int> onionFrames() const;
    QStringList onionExcludeProperties() const;
    QString onionLayerTag() const;
    AnimeTimelineState timelineState() const;

public slots:
    // 状态栏消息
    void setStatusMessage(const QString &message);

    // 活动画布与目标切换
    void setActiveViewName(const QString &viewName);
    void setChangeableTimeline(bool changeable);
    void setChangeableTexture(bool changeable);
    void updateAttention(const QString &viewName,
                         AttentionChange change,
                         int frame,
                         int layer,
                         int asset);

    // 项目新建打开保存
    void newProject(const QSize &canvasSize);
    void applyNewCanvasSize(const QSize &size);
    bool saveProjectTo(const QString &fileName, QString *errorOut = nullptr);
    bool loadProjectFrom(const QString &fileName, QString *errorOut = nullptr);
    bool openTextureViewFrom(const QString &fileName, QString *errorOut = nullptr);
    bool saveTextureViewTo(const QString &fileName, QString *errorOut = nullptr);

    // 图层增删移动显示隐藏
    int addLayer(const QString &viewName = QStringLiteral("main"));
    bool deleteLayer(int layerIndex, const QString &viewName = QStringLiteral("main"));
    int deleteLayerGroup(int groupId, const QString &viewName = QStringLiteral("main"));
    bool dissolveLayerGroup(int groupId, const QString &viewName = QStringLiteral("main"));
    bool moveLayer(int fromIndex, int toIndex, const QString &viewName = QStringLiteral("main"));
    void setLayerGroupCollapsed(int groupId,
                                bool collapsed,
                                const QString &viewName = QStringLiteral("main"));
    bool setLayerVisible(int layerIndex,
                         bool visible,
                         const QString &viewName = QStringLiteral("main"));
    void selectLayer(int layerIndex, const QString &viewName = QStringLiteral("main"));
    void unselectLayer(const QString &viewName = QStringLiteral("main"));

    // 当前工具和工具选项
    bool applyTool(int tool, bool reloadOptions = true);
    void selectTool(int tool);
    void selectExtraTool(const AnimeExtraToolDefinition &tool);
    bool selectExtraToolByName(const QString &extraToolName);
    void selectColor(const QColor &color);
    void setDrawingColor(const QColor &color);
    void setFillScope(int scope);
    void setFillAllLayers(bool allLayers);
    void selectEraserMode(int tool);
    void setSmoothValue(int value);
    void setPenWidth(int width);
    void setDrawSettings(int stabilizer, int simplify, int corner);
    void sendToolOption(const QString &hook,
                        const QString &name,
                        const QString &type,
                        const QVariant &value,
                        int row = -1,
                        int startColumn = -1,
                        int endColumn = -1);
    void sendPadEvent(const QString &pad, const QString &phase, double x, double y);
    void applyLockedTools(const QString &viewName, const QStringList &tools);
    void refreshToolLockState();
    void refreshToolOptions();
    void refreshExtraToolOptions();

    // 素材
    int addAsset(AnimeColumnType type = AnimeColumnType::Vector,
                 const QString &name = QString(),
                 const QString &viewName = QString());
    void selectAsset(int assetIndex, const QString &viewName = QString());
    void unselectAsset(const QString &viewName = QString());
    int addLayerForAsset(int assetIndex, const QString &viewName = QString());
    bool assignAssetToLayer(int layerIndex,
                            int assetIndex,
                            const QString &viewName = QString());

    // 历史
    void commitHistory(const QString &label, const QString &viewName = QString());
    void resetHistory(const QString &label, const QString &viewName = QString());
    void handleViewHistoryCommitted(const QString &viewName);
    bool undo(const QString &viewName = QString());
    bool redo(const QString &viewName = QString());
    bool goToHistory(int row, const QString &viewName = QString());
    void applyHistoryRestore(const QString &viewName);

    // 时间轴和播放与洋葱皮
    void selectFrame(int frameIndex);
    void stepFrame(int delta);
    void previousFrame();
    void nextFrame();
    int addFrame();
    int addHoldFrame();
    int duplicateFrame();
    bool deleteFrame();
    bool moveFrame(int fromIndex, int toIndex);
    void setFps(int fps);

    void startPlayback();
    void advancePlaybackFrame();
    void stopPlayback();
    void setPlaybackLoop(bool loop);

    void setOnionEnabled(bool on);
    void setOnionLines(bool on);
    void setOnionFills(bool on);
    void setOnionAmLayers(bool on);
    void setOnionGuideLines(bool on);
    void toggleOnionFrame(int frame, bool on);
    void pullOnionPolicy();

    // 嵌入式 Python 状态同步与脚本执行 (Embedded Python State Sync & Execution)
    void syncEmbeddedPythonState();
    static QString resolvePythonScriptPath(const QString &scriptName);
    QString runEmbeddedPythonCommand(const QString &command, const QString &scriptDir = QString());
    QString runPythonInitializationScript(QString *outputOut = nullptr);

signals:
    void statusMessageChanged(const QString &message);
    void pythonDebugMessage(const QString &message);
    void projectFilePathChanged(const QString &filePath);
    void childFilePathChanged(const QString &filePath);
    void windowTitleChanged(const QString &title);
    void canvasSizeChanged(const QString &viewName, const QSize &size);

    void activeViewChanged(const QString &viewName);
    void mainPaintViewObjectChanged(QObject *viewObject);
    void childPaintViewObjectChanged(QObject *viewObject);
    void activePaintViewObjectChanged(QObject *viewObject);
    void requestSurfaceTextureView();
    void requestAutoFitTextureControlView();
    void requestClearTimelineThumbnails();

    void attentionUpdated(const QString &viewName,
                          bool frameChanged,
                          bool layerChanged,
                          bool assetChanged);
    void layersChanged(const QString &viewName, int selectedLayer);
    void assetsChanged(const QString &viewName, int selectedAsset);
    void timelineChanged();
    void playbackStateChanged(bool active, int frameIndex, const QString &viewName);
    void onionStateChanged();

    void toolChanged(int tool, const QString &extraToolName);
    void toolLockStateChanged();
    void toolOptionsLayoutChanged(const QJsonObject &layout, int tool, bool isExtraTool);
    void toolOptionsStateChanged();
    void drawingColorChanged(const QColor &color);

    void historyChanged();
    void historyRestored(const QString &viewName);
    void pythonStateSyncRequested();

private:
    struct HeadlessViewState {
        int tool = int(Tool::Pen);
        QString strokeProperty;
        QString activePythonTool;
        int pythonNotifiedFrame = -1;
        int pythonNotifiedLayer = -1;
        int pythonNotifiedLayerId = -1;
    };

    QString resolveViewName(const QString &viewName) const;
    AnimePaintViewAdapter *adapterFor(const QString &viewName);
    const AnimePaintViewAdapter *adapterFor(const QString &viewName) const;
    void setHeadlessViewTool(const QString &viewName, int tool);
    void setHeadlessViewStrokeProperty(const QString &viewName, const QString &property);
    void notifyHeadlessFrameChangedIfNeeded(const QString &viewName);
    void notifyHeadlessLayerChangedIfNeeded(const QString &viewName);
    void notifyHeadlessHistoryRestore(const QString &viewName);
    void dispatchHeadlessHandleHook(const QString &viewName,
                                    const QString &phase);
    void dispatchHeadlessExtraToolHook(const QString &viewName,
                                       const QString &name,
                                       const QString &property);
    void dispatchHeadlessToolOptionHook(const QString &viewName,
                                        const QString &hook,
                                        const QString &name,
                                        const QString &type,
                                        const QVariant &value,
                                        int row,
                                        int startColumn,
                                        int endColumn);
    void dispatchHeadlessPadHook(const QString &viewName,
                                 const QString &pad,
                                 const QString &phase,
                                 double x,
                                 double y);
    bool dispatchHeadlessLayerVisibilityHook(const QString &viewName,
                                             int layerIndex,
                                             bool visible);

    AnimeSceneModel m_internalMainModel;
    AnimeSceneModel m_internalChildModel;
    SceneHistory m_internalMainHistory;
    SceneHistory m_internalChildHistory;
    QObject *m_internalMainViewObject = nullptr;
    QObject *m_internalChildViewObject = nullptr;

    QHash<QString, AnimePaintViewAdapter> m_adapters;
    QHash<QString, SelectionAttention> m_attentionByView;
    QHash<QString, HeadlessViewState> m_headlessViewState;
    int m_lastAutoFitChildFrame = -1;

    QString m_activeViewName = QStringLiteral("main");
    bool m_changeableTimeline = false;
    bool m_changeableTexture = false;

    QString m_statusMessage;
    QString m_currentFilePath;
    QString m_childFilePath;

    int m_currentTool = int(Tool::Pen);
    QString m_activeExtraTool;
    QString m_activeExtraToolProperty;
    int m_toolSmoothValue = 50;
    int m_toolPenWidth = 5;
    bool m_toolFillAllLayers = false;
    QColor m_drawingColor = Qt::black;
    AnimeStrokeFitSettings m_strokeFitSettings;
    QVector<AnimeExtraToolDefinition> m_extraTools;
    QHash<QString, QSet<QString>> m_lockedToolsByView;

    QTimer *m_playbackTimer = nullptr;
    QString m_playbackViewName;
    int m_playbackIndex = 0;
    int m_playbackFrameCount = 0;
    bool m_playbackLoop = true;

    bool m_onionEnabled = false;
    bool m_onionGuideLines = false;
    bool m_onionLines = true;
    bool m_onionFills = true;
    bool m_onionAmLayers = true;
    QSet<int> m_onionFrames;
    QStringList m_onionExcludeProperties;
    QString m_onionLayerTag;
};

#endif // ANIMEANBUSINESS_H
