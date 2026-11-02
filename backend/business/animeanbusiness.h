// Source: Copied from C:/Users/admin/Documents/VibeFrontend/animean/business/animeanbusiness.h
#ifndef ANIMEANBUSINESS_H
#define ANIMEANBUSINESS_H

#include "algorithm/core/path.h"
#include <cstdint>
#include <string>

#include "algorithm/animemodel.h"
#include "algorithm/scenehistory.h"
#include "algorithm/vectorlogic.h"
#include "selectionattention.h"

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/file.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/json.h"
#include "algorithm/core/painter.h"
#include "algorithm/core/string.h"
#include "platform/signalslot.h"
#include "platform/timer.h"

#include <functional>
#include <utility>

struct AnimeLayerPanelRow {
    bool group = false;
    int layerIndex = -1;
    int groupId = 0;
    AnimeCore::String name;
    bool visible = true;
    bool collapsed = false;
    int depth = 0;
    bool child = false;
    bool parent = false;
};

struct AnimeExtraToolDefinition {
    AnimeCore::String name;
    AnimeCore::String title;
    AnimeCore::String property;
    AnimeCore::String handler;
    AnimeCore::String baseTool;
    AnimeCore::String page = AnimeCore::String("mapping");
};

struct AnimeTimelineState {
    AnimeCore::String viewName;
    int frameCount = 0;
    int currentFrame = 0;
    AnimeCore::Vector<bool> holds;
    AnimeCore::Vector<AnimeCore::String> frameNames;
    int fps = 12;
    bool playbackActive = false;
    bool loop = true;
    bool onionAvailable = true;
    bool onionEnabled = false;
    bool onionLines = true;
    bool onionFills = true;
    bool onionAmLayers = true;
    bool onionGuideLines = false;
    AnimeCore::Set<int> onionFrames;
};

struct AnimePaintViewAdapter {
    void *viewObject = nullptr;
    AnimeSceneModel *model = nullptr;
    SceneHistory *history = nullptr;

    std::function<void()> modelReplaced;
    std::function<void(const AnimeCore::Size &)> setCanvasSize;
    std::function<void()> requestUpdate;
    std::function<void(int)> setCurrentFrame;
    std::function<void(int)> setCurrentLayer;
    std::function<void(int)> setCurrentAsset;
    std::function<void()> notifyLayerChanged;
    std::function<void()> removeInvalidFillRegions;

    std::function<void(const AnimeCore::String &)> commitHistory;
    std::function<void(const AnimeCore::String &)> resetHistory;
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

    std::function<int(AnimeColumnType, const AnimeCore::String &)> addAsset;
    std::function<bool(int, int)> assignAssetToLayer;
    std::function<int(int)> addLayerForAsset;

    std::function<void(int)> setTool;
    std::function<int()> currentTool;
    std::function<void(const AnimeCore::String &)> setStrokeProperty;
    std::function<void(const AnimeCore::Color &)> setDrawingColor;
    std::function<void(double)> setPenWidth;
    std::function<void(int)> setFillScope;
    std::function<void(int)> setSmoothValue;
    std::function<void(const AnimeStrokeFitSettings &)> setStrokeFitSettings;
    std::function<void(const AnimeCore::String &, const AnimeCore::String &)> sendExtraToolMessage;
    std::function<void(const AnimeCore::String &, const AnimeCore::String &, const AnimeCore::String &, const AnimeCore::JsonVariant &, int, int, int)> sendToolOptionMessage;
    std::function<void(const AnimeCore::String &, const AnimeCore::String &, double, double)> sendPadMessage;

    std::function<bool(int, AnimeCore::String *)> buildPlaybackCache;
    std::function<void(int)> showPlaybackFrame;
    std::function<void()> endPlayback;

    std::function<void(bool)> setOnionEnabled;
    std::function<void(const AnimeCore::Set<int> &)> setOnionFrames;
    std::function<void(bool)> setOnionLines;
    std::function<void(bool)> setOnionFills;
    std::function<void(bool)> setOnionAmLayers;
    std::function<void(bool)> setOnionGuideLines;
    std::function<void(const AnimeCore::StringList &)> setOnionExcludeProperties;
    std::function<void(const AnimeCore::String &)> setOnionLayerTag;

    std::function<AnimeCore::Image(int, const AnimeCore::Size &)> renderFrameThumbnail;
};

class AnimeanBusiness
{
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

    enum class FillScope {
        CurrentLayer = 0,
        AllLayers = 1
    };

    AnimeanBusiness();
    ~AnimeanBusiness();

    // --- 绘图区提供方式 (Paint View Provision & Binding) ---
    void bindPaintView(const AnimeCore::String &viewName, const AnimePaintViewAdapter &adapter);
    void unbindPaintView(const AnimeCore::String &viewName);
    void *paintViewObject(const AnimeCore::String &viewName) const;
    void *mainPaintViewObject() const;
    void *childPaintViewObject() const;
    void *activePaintViewObject() const;

    AnimeCore::String activeViewName() const;
    AnimeCore::String framePanelTargetViewName() const;
    AnimeCore::String layerPanelTargetViewName() const;
    AnimeCore::String assetPanelTargetViewName() const;
    bool changeableTimeline() const;
    bool changeableTexture() const;

    AnimeSceneModel &modelForView(const AnimeCore::String &viewName = AnimeCore::String("main"));
    const AnimeSceneModel &modelForView(const AnimeCore::String &viewName = AnimeCore::String("main")) const;
    SceneHistory &historyForView(const AnimeCore::String &viewName = AnimeCore::String("main"));
    const SceneHistory &historyForView(const AnimeCore::String &viewName = AnimeCore::String("main")) const;

    SelectionAttention &attentionForView(const AnimeCore::String &viewName);
    SelectionAttention attentionForView(const AnimeCore::String &viewName) const;
    void syncAllViewAttentionsFromModels();

    AnimeCore::Image renderFrameThumbnail(int frameIndex,
                                          const AnimeCore::Size &size,
                                          const AnimeCore::String &viewName = AnimeCore::String());

    // --- 状态栏消息 (Status Bar Message) ---
    AnimeCore::String statusMessage() const;

    // --- 项目新建打开保存 (Project New / Open / Save) ---
    AnimeCore::String currentFilePath() const;
    void setCurrentFilePath(const AnimeCore::String &filePath);
    AnimeCore::String childFilePath() const;
    void setChildFilePath(const AnimeCore::String &filePath);
    AnimeCore::String windowTitle() const;
    AnimeCore::Size canvasSize(const AnimeCore::String &viewName = AnimeCore::String("main")) const;

    static AnimeCore::String ensureProjectFileExtension(const AnimeCore::String &fileName);
    static AnimeCore::String ensureTextureViewFileExtension(const AnimeCore::String &fileName);
    static AnimeCore::String projectOpenFilter();
    static AnimeCore::String projectSaveFilter();
    static AnimeCore::String textureViewOpenFilter();
    static AnimeCore::String textureViewSaveFilter();

    void installLoadedModel(const AnimeCore::String &viewName, const AnimeSceneModel &model);
    void activateLoadedModel(const AnimeCore::String &viewName, const AnimeCore::String &historyLabel);
    void adoptLoadedModel(const AnimeCore::String &viewName,
                          const AnimeSceneModel &model,
                          const AnimeCore::String &historyLabel);

    // --- 图层增删移动显示隐藏 (Layers) ---
    int layerCount(const AnimeCore::String &viewName = AnimeCore::String("main")) const;
    int currentLayer(const AnimeCore::String &viewName = AnimeCore::String("main")) const;
    AnimeCore::String layerName(int layerIndex, const AnimeCore::String &viewName = AnimeCore::String("main")) const;
    bool isLayerVisible(int layerIndex, const AnimeCore::String &viewName = AnimeCore::String("main")) const;
    AnimeCore::Vector<AnimeLayerPanelRow> layerRowsForView(const AnimeCore::String &viewName = AnimeCore::String("main")) const;
    int applyLayerPanelStructure(const AnimeCore::String &viewName,
                                 const AnimeCore::Vector<AnimeLayerPanelRow> &rows,
                                 int movedColumnId = 0);
    int reorderLayerTree(const AnimeCore::String &viewName,
                         const AnimeCore::Vector<AnimeLayerNode> &capturedTree,
                         int movedColumnId,
                         const AnimeCore::Vector<std::pair<int, int>> &flatGroupAndLayerRoles);

    // --- 当前工具和工具选项（含 toolcontrol.options_for_tool_json） (Tools & Tool Options) ---
    static AnimeCore::String toolControlName(Tool tool);
    static AnimeCore::String toolLockName(Tool tool);
    static AnimeCore::Vector<Tool> lockableTools();

    int currentTool() const;
    Tool currentToolEnum() const;
    AnimeCore::String activeExtraTool() const;
    AnimeCore::String activeExtraToolProperty() const;
    int toolSmoothValue() const;
    int toolPenWidth() const;
    bool toolFillAllLayers() const;
    FillScope toolFillScope() const;
    AnimeCore::Color drawingColor() const;
    AnimeStrokeFitSettings strokeFitSettings() const;

    AnimeCore::Vector<AnimeExtraToolDefinition> loadExtraTools();
    AnimeCore::Vector<AnimeExtraToolDefinition> extraTools() const;
    static AnimeCore::Vector<AnimeExtraToolDefinition> parseExtraToolsJson(const AnimeCore::JsonArray &array);

    AnimeCore::String optionsForToolJson(Tool tool) const;
    AnimeCore::String optionsForToolJsonByName(const AnimeCore::String &toolName) const;
    AnimeCore::JsonObject toolOptionsLayout(Tool tool) const;
    AnimeCore::String optionsForExtraToolJson(const AnimeCore::String &extraToolName) const;
    AnimeCore::JsonObject extraToolOptionsLayout(const AnimeCore::String &extraToolName) const;
    static AnimeCore::JsonObject loadBuiltInToolLayout(Tool tool,
                                                       int smoothValue,
                                                       int penWidth,
                                                       FillScope fillScope);

    bool isToolLocked(int tool) const;
    AnimeCore::Set<AnimeCore::String> lockedToolsForView(const AnimeCore::String &viewName) const;

    // --- 素材 (Assets) ---
    int assetCount(const AnimeCore::String &viewName = AnimeCore::String()) const;
    int currentAsset(const AnimeCore::String &viewName = AnimeCore::String()) const;
    AnimeCore::String assetName(int assetIndex, const AnimeCore::String &viewName = AnimeCore::String()) const;
    AnimeCore::StringList visibleAssetNames(const AnimeCore::String &viewName = AnimeCore::String()) const;

    // --- 历史 (History) ---
    AnimeCore::String undoTargetViewName() const;
    AnimeCore::String redoTargetViewName() const;
    bool canUndo() const;
    bool canRedo() const;
    AnimeCore::StringList historyLabels(const AnimeCore::String &viewName = AnimeCore::String()) const;
    int currentHistoryIndex(const AnimeCore::String &viewName = AnimeCore::String()) const;

    // --- 时间轴和播放 (Timeline, Playback & Onion Skin) ---
    int frameCount(const AnimeCore::String &viewName = AnimeCore::String()) const;
    int currentFrame(const AnimeCore::String &viewName = AnimeCore::String()) const;
    int playbackFps(const AnimeCore::String &viewName = AnimeCore::String()) const;
    bool isPlaybackActive() const;
    int playbackIndex() const;
    AnimeCore::String playbackViewName() const;
    bool isPlaybackLoop() const;

    bool isOnionEnabled() const;
    bool isOnionLines() const;
    bool isOnionFills() const;
    bool isOnionAmLayers() const;
    bool isOnionGuideLines() const;
    AnimeCore::Set<int> onionFrames() const;
    AnimeCore::StringList onionExcludeProperties() const;
    AnimeCore::String onionLayerTag() const;
    AnimeTimelineState timelineState() const;

    // 状态栏消息
    void setStatusMessage(const AnimeCore::String &message);

    // 活动画布与目标切换
    void setActiveViewName(const AnimeCore::String &viewName);
    void setChangeableTimeline(bool changeable);
    void setChangeableTexture(bool changeable);
    void updateAttention(const AnimeCore::String &viewName,
                         AttentionChange change,
                         int frame,
                         int layer,
                         int asset);

    // 项目新建打开保存
    void newProject(const AnimeCore::Size &canvasSize);
    void applyNewCanvasSize(const AnimeCore::Size &size);
    bool saveProjectTo(const AnimeCore::String &fileName, AnimeCore::String *errorOut = nullptr);
    bool loadProjectFrom(const AnimeCore::String &fileName, AnimeCore::String *errorOut = nullptr);
    bool openTextureViewFrom(const AnimeCore::String &fileName, AnimeCore::String *errorOut = nullptr);
    bool saveTextureViewTo(const AnimeCore::String &fileName, AnimeCore::String *errorOut = nullptr);

    // 图层增删移动显示隐藏
    int addLayer(const AnimeCore::String &viewName = AnimeCore::String("main"));
    bool deleteLayer(int layerIndex, const AnimeCore::String &viewName = AnimeCore::String("main"));
    int deleteLayerGroup(int groupId, const AnimeCore::String &viewName = AnimeCore::String("main"));
    bool dissolveLayerGroup(int groupId, const AnimeCore::String &viewName = AnimeCore::String("main"));
    bool moveLayer(int fromIndex, int toIndex, const AnimeCore::String &viewName = AnimeCore::String("main"));
    void setLayerGroupCollapsed(int groupId,
                                bool collapsed,
                                const AnimeCore::String &viewName = AnimeCore::String("main"));
    bool setLayerVisible(int layerIndex,
                         bool visible,
                         const AnimeCore::String &viewName = AnimeCore::String("main"));
    void selectLayer(int layerIndex, const AnimeCore::String &viewName = AnimeCore::String("main"));
    void unselectLayer(const AnimeCore::String &viewName = AnimeCore::String("main"));

    // 当前工具和工具选项
    bool applyTool(int tool, bool reloadOptions = true);
    void selectTool(int tool);
    void selectExtraTool(const AnimeExtraToolDefinition &tool);
    bool selectExtraToolByName(const AnimeCore::String &extraToolName);
    void selectColor(const AnimeCore::Color &color);
    void setDrawingColor(const AnimeCore::Color &color);
    void setFillScope(int scope);
    void setFillAllLayers(bool allLayers);
    void selectEraserMode(int tool);
    void setSmoothValue(int value);
    void setPenWidth(int width);
    void setDrawSettings(int stabilizer, int simplify, int corner);
    void sendToolOption(const AnimeCore::String &hook,
                        const AnimeCore::String &name,
                        const AnimeCore::String &type,
                        const AnimeCore::JsonVariant &value,
                        int row = -1,
                        int startColumn = -1,
                        int endColumn = -1);
    void sendPadEvent(const AnimeCore::String &pad, const AnimeCore::String &phase, double x, double y);
    void applyLockedTools(const AnimeCore::String &viewName, const AnimeCore::StringList &tools);
    void refreshToolLockState();
    void refreshToolOptions();
    void refreshExtraToolOptions();

    // 素材
    int addAsset(AnimeColumnType type = AnimeColumnType::Vector,
                 const AnimeCore::String &name = AnimeCore::String(),
                 const AnimeCore::String &viewName = AnimeCore::String());
    void selectAsset(int assetIndex, const AnimeCore::String &viewName = AnimeCore::String());
    void unselectAsset(const AnimeCore::String &viewName = AnimeCore::String());
    int addLayerForAsset(int assetIndex, const AnimeCore::String &viewName = AnimeCore::String());
    bool assignAssetToLayer(int layerIndex,
                            int assetIndex,
                            const AnimeCore::String &viewName = AnimeCore::String());

    // 历史
    void commitHistory(const AnimeCore::String &label, const AnimeCore::String &viewName = AnimeCore::String());
    void resetHistory(const AnimeCore::String &label, const AnimeCore::String &viewName = AnimeCore::String());
    void handleViewHistoryCommitted(const AnimeCore::String &viewName);
    bool undo(const AnimeCore::String &viewName = AnimeCore::String());
    bool redo(const AnimeCore::String &viewName = AnimeCore::String());
    bool goToHistory(int row, const AnimeCore::String &viewName = AnimeCore::String());
    void applyHistoryRestore(const AnimeCore::String &viewName);

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
    static AnimeCore::String resolvePythonScriptPath(const AnimeCore::String &scriptName);
    AnimeCore::String runEmbeddedPythonCommand(const AnimeCore::String &command, const AnimeCore::String &scriptDir = AnimeCore::String());
    AnimeCore::String runPythonInitializationScript(AnimeCore::String *outputOut = nullptr);

    Signal<AnimeCore::String> statusMessageChanged;
    Signal<AnimeCore::String> pythonDebugMessage;
    Signal<AnimeCore::String> projectFilePathChanged;
    Signal<AnimeCore::String> childFilePathChanged;
    Signal<AnimeCore::String> windowTitleChanged;
    Signal<AnimeCore::String, AnimeCore::Size> canvasSizeChanged;

    Signal<AnimeCore::String> activeViewChanged;
    Signal<void *> mainPaintViewObjectChanged;
    Signal<void *> childPaintViewObjectChanged;
    Signal<void *> activePaintViewObjectChanged;
    Signal<> requestSurfaceTextureView;
    Signal<> requestAutoFitTextureControlView;
    Signal<> requestClearTimelineThumbnails;

    Signal<AnimeCore::String, bool, bool, bool> attentionUpdated;
    Signal<AnimeCore::String, int> layersChanged;
    Signal<AnimeCore::String, int> assetsChanged;
    Signal<> timelineChanged;
    Signal<bool, int, AnimeCore::String> playbackStateChanged;
    Signal<> onionStateChanged;

    Signal<int, AnimeCore::String> toolChanged;
    Signal<> toolLockStateChanged;
    Signal<AnimeCore::JsonObject, int, bool> toolOptionsLayoutChanged;
    Signal<> toolOptionsStateChanged;
    Signal<AnimeCore::Color> drawingColorChanged;

    Signal<> historyChanged;
    Signal<AnimeCore::String> historyRestored;
    Signal<> pythonStateSyncRequested;

private:
    struct HeadlessViewState {
        int tool = int(Tool::Pen);
        AnimeCore::String strokeProperty;
        AnimeCore::String activePythonTool;
        int pythonNotifiedFrame = -1;
        int pythonNotifiedLayer = -1;
        int pythonNotifiedLayerId = -1;
    };

    AnimeCore::String resolveViewName(const AnimeCore::String &viewName) const;
    AnimePaintViewAdapter *adapterFor(const AnimeCore::String &viewName);
    const AnimePaintViewAdapter *adapterFor(const AnimeCore::String &viewName) const;
    void setHeadlessViewTool(const AnimeCore::String &viewName, int tool);
    void setHeadlessViewStrokeProperty(const AnimeCore::String &viewName, const AnimeCore::String &property);
    void notifyHeadlessFrameChangedIfNeeded(const AnimeCore::String &viewName);
    void notifyHeadlessLayerChangedIfNeeded(const AnimeCore::String &viewName);
    void notifyHeadlessHistoryRestore(const AnimeCore::String &viewName);
    void dispatchHeadlessHandleHook(const AnimeCore::String &viewName,
                                    const AnimeCore::String &phase);
    void dispatchHeadlessExtraToolHook(const AnimeCore::String &viewName,
                                       const AnimeCore::String &name,
                                       const AnimeCore::String &property);
    void dispatchHeadlessToolOptionHook(const AnimeCore::String &viewName,
                                        const AnimeCore::String &hook,
                                        const AnimeCore::String &name,
                                        const AnimeCore::String &type,
                                        const AnimeCore::JsonVariant &value,
                                        int row,
                                        int startColumn,
                                        int endColumn);
    void dispatchHeadlessPadHook(const AnimeCore::String &viewName,
                                 const AnimeCore::String &pad,
                                 const AnimeCore::String &phase,
                                 double x,
                                 double y);
    bool dispatchHeadlessLayerVisibilityHook(const AnimeCore::String &viewName,
                                             int layerIndex,
                                             bool visible);

    AnimeSceneModel m_internalMainModel;
    AnimeSceneModel m_internalChildModel;
    SceneHistory m_internalMainHistory;
    SceneHistory m_internalChildHistory;
    int m_internalMainViewToken = 0;
    int m_internalChildViewToken = 0;
    void *m_internalMainViewObject = &m_internalMainViewToken;
    void *m_internalChildViewObject = &m_internalChildViewToken;

    AnimeCore::Hash<AnimeCore::String, AnimePaintViewAdapter> m_adapters;
    AnimeCore::Hash<AnimeCore::String, SelectionAttention> m_attentionByView;
    AnimeCore::Hash<AnimeCore::String, HeadlessViewState> m_headlessViewState;
    int m_lastAutoFitChildFrame = -1;

    AnimeCore::String m_activeViewName = AnimeCore::String("main");
    bool m_changeableTimeline = false;
    bool m_changeableTexture = false;

    AnimeCore::String m_statusMessage;
    AnimeCore::String m_currentFilePath;
    AnimeCore::String m_childFilePath;

    int m_currentTool = int(Tool::Pen);
    AnimeCore::String m_activeExtraTool;
    AnimeCore::String m_activeExtraToolProperty;
    int m_toolSmoothValue = 50;
    int m_toolPenWidth = 5;
    bool m_toolFillAllLayers = false;
    AnimeCore::Color m_drawingColor = AnimeCore::Color(0, 0, 0);
    AnimeStrokeFitSettings m_strokeFitSettings;
    AnimeCore::Vector<AnimeExtraToolDefinition> m_extraTools;
    AnimeCore::Hash<AnimeCore::String, AnimeCore::Set<AnimeCore::String>> m_lockedToolsByView;

    Timer *m_playbackTimer = nullptr;
    AnimeCore::String m_playbackViewName;
    int m_playbackIndex = 0;
    int m_playbackFrameCount = 0;
    bool m_playbackLoop = true;

    bool m_onionEnabled = false;
    bool m_onionGuideLines = false;
    bool m_onionLines = true;
    bool m_onionFills = true;
    bool m_onionAmLayers = true;
    AnimeCore::Set<int> m_onionFrames;
    AnimeCore::StringList m_onionExcludeProperties;
    AnimeCore::String m_onionLayerTag;
};

#endif // ANIMEANBUSINESS_H
