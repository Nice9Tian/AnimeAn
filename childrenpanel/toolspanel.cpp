#include "toolspanel.h"
#include "ui_toolspanel.h"

#include <QPushButton>
#include <QSignalBlocker>
#include <QtGlobal>
#include <QVBoxLayout>

ToolsPanel::ToolsPanel(QWidget *parent, bool showBuiltIns)
    : QWidget(parent)
    , ui(new Ui::ToolsPanel)
    , m_showBuiltIns(showBuiltIns)
{
    ui->setupUi(this);

    m_layout = new QVBoxLayout(this);
    m_arrowButton = new QPushButton(QStringLiteral("Arrow"), this);
    m_arrowButton->setCheckable(true);
    m_arrowButton->setCursor(ui->penButton->cursor());
    m_arrowButton->setMinimumSize(ui->penButton->minimumSize());
    m_arrowButton->setStyleSheet(ui->penButton->styleSheet());
    m_connectButton = new QPushButton(QStringLiteral("Connect"), this);
    m_connectButton->setCheckable(true);
    m_connectButton->setCursor(ui->penButton->cursor());
    m_connectButton->setMinimumSize(ui->penButton->minimumSize());
    m_connectButton->setStyleSheet(ui->penButton->styleSheet());
    m_transferButton = new QPushButton(QStringLiteral("Transfer"), this);
    m_transferButton->setCheckable(true);
    m_transferButton->setCursor(ui->penButton->cursor());
    m_transferButton->setMinimumSize(ui->penButton->minimumSize());
    m_transferButton->setStyleSheet(ui->penButton->styleSheet());

    if (m_showBuiltIns) {
        m_layout->addWidget(m_arrowButton);
        m_layout->addWidget(ui->penButton);
        m_layout->addWidget(ui->eraserButton);
        m_layout->addWidget(ui->fillButton);
        m_layout->addWidget(m_transferButton);
        m_layout->addWidget(m_connectButton);
    } else {
        // The .ui parents its three buttons to this widget with absolute
        // geometry, so leaving them out of the layout is not enough to keep
        // them off the page - they have to be hidden. They stay alive because
        // setExtraTools still copies its look from penButton, and setTool
        // still blocks and clears them.
        for (QPushButton *button : {m_arrowButton, m_connectButton, m_transferButton,
                                    ui->penButton, ui->eraserButton, ui->fillButton}) {
            button->hide();
        }
    }
    m_layout->addStretch();

    ui->penButton->setCheckable(true);
    ui->eraserButton->setCheckable(true);
    ui->fillButton->setCheckable(true);
    if (m_showBuiltIns) {
        setTool(PaintOpenGLWidget::Tool::Pen);
    }

    connect(m_arrowButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Arrow);
        emit toolSelected(PaintOpenGLWidget::Tool::Arrow);
    });
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Connect);
        emit toolSelected(PaintOpenGLWidget::Tool::Connect);
    });
    connect(m_transferButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Transfer);
        emit toolSelected(PaintOpenGLWidget::Tool::Transfer);
    });

    connect(ui->penButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Pen);
        emit toolSelected(PaintOpenGLWidget::Tool::Pen);
    });
    connect(ui->eraserButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Eraser);
        emit toolSelected(PaintOpenGLWidget::Tool::Eraser);
    });
    connect(ui->fillButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Fill);
        emit toolSelected(PaintOpenGLWidget::Tool::Fill);
    });
}

ToolsPanel::~ToolsPanel()
{
    delete ui;
}

void ToolsPanel::setTool(PaintOpenGLWidget::Tool tool)
{
    const QSignalBlocker arrowBlocker(m_arrowButton);
    const QSignalBlocker connectBlocker(m_connectButton);
    const QSignalBlocker transferBlocker(m_transferButton);
    const QSignalBlocker penBlocker(ui->penButton);
    const QSignalBlocker eraserBlocker(ui->eraserButton);
    const QSignalBlocker fillBlocker(ui->fillButton);

    m_arrowButton->setChecked(tool == PaintOpenGLWidget::Tool::Arrow);
    m_connectButton->setChecked(tool == PaintOpenGLWidget::Tool::Connect);
    m_transferButton->setChecked(tool == PaintOpenGLWidget::Tool::Transfer);
    ui->penButton->setChecked(tool == PaintOpenGLWidget::Tool::Pen);
    ui->eraserButton->setChecked(tool == PaintOpenGLWidget::Tool::Eraser ||
                                 tool == PaintOpenGLWidget::Tool::DeleteLine ||
                                 tool == PaintOpenGLWidget::Tool::CutLine);
    ui->fillButton->setChecked(tool == PaintOpenGLWidget::Tool::Fill);
    for (QPushButton *button : m_extraButtons) {
        if (button) {
            button->setChecked(false);
        }
    }
}

void ToolsPanel::clearSelection()
{
    const QSignalBlocker arrowBlocker(m_arrowButton);
    const QSignalBlocker connectBlocker(m_connectButton);
    const QSignalBlocker transferBlocker(m_transferButton);
    const QSignalBlocker penBlocker(ui->penButton);
    const QSignalBlocker eraserBlocker(ui->eraserButton);
    const QSignalBlocker fillBlocker(ui->fillButton);

    m_arrowButton->setChecked(false);
    m_connectButton->setChecked(false);
    m_transferButton->setChecked(false);
    ui->penButton->setChecked(false);
    ui->eraserButton->setChecked(false);
    ui->fillButton->setChecked(false);
    for (QPushButton *button : m_extraButtons) {
        if (button) {
            button->setChecked(false);
        }
    }
}

void ToolsPanel::setExtraTools(const QVector<ExtraToolDefinition> &tools)
{
    for (QPushButton *button : m_extraButtons) {
        if (button) {
            button->deleteLater();
        }
    }
    m_extraButtons.clear();
    m_extraTools = tools;

    const int insertIndex = m_layout ? qMax(0, m_layout->count() - 1) : 0;
    for (int index = 0; index < m_extraTools.size(); ++index) {
        const ExtraToolDefinition definition = m_extraTools.at(index);
        QPushButton *button = new QPushButton(definition.title.isEmpty() ? definition.name : definition.title, this);
        button->setCheckable(true);
        button->setCursor(ui->penButton->cursor());
        button->setMinimumSize(ui->penButton->minimumSize());
        button->setStyleSheet(ui->penButton->styleSheet());
        connect(button, &QPushButton::clicked, this, [this, index]() {
            const QSignalBlocker arrowBlocker(m_arrowButton);
            const QSignalBlocker connectBlocker(m_connectButton);
            const QSignalBlocker transferBlocker(m_transferButton);
            const QSignalBlocker penBlocker(ui->penButton);
            const QSignalBlocker eraserBlocker(ui->eraserButton);
            const QSignalBlocker fillBlocker(ui->fillButton);
            m_arrowButton->setChecked(false);
            m_connectButton->setChecked(false);
            m_transferButton->setChecked(false);
            ui->penButton->setChecked(false);
            ui->eraserButton->setChecked(false);
            ui->fillButton->setChecked(false);
            for (int buttonIndex = 0; buttonIndex < m_extraButtons.size(); ++buttonIndex) {
                const QSignalBlocker blocker(m_extraButtons[buttonIndex]);
                m_extraButtons[buttonIndex]->setChecked(buttonIndex == index);
            }
            emit extraToolSelected(m_extraTools.at(index));
        });
        m_extraButtons.append(button);
        m_layout->insertWidget(insertIndex + index, button);
    }
}
