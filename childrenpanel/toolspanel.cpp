#include "toolspanel.h"
#include "ui_toolspanel.h"

#include <QPushButton>
#include <QSignalBlocker>
#include <QtGlobal>
#include <QVBoxLayout>

ToolsPanel::ToolsPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolsPanel)
{
    ui->setupUi(this);

    m_layout = new QVBoxLayout(this);
    m_arrowButton = new QPushButton(QStringLiteral("Arrow"), this);
    m_arrowButton->setCheckable(true);
    m_arrowButton->setCursor(ui->penButton->cursor());
    m_arrowButton->setMinimumSize(ui->penButton->minimumSize());
    m_arrowButton->setStyleSheet(ui->penButton->styleSheet());
    m_layout->addWidget(m_arrowButton);
    m_layout->addWidget(ui->penButton);
    m_layout->addWidget(ui->moveButton);
    m_layout->addWidget(ui->eraserButton);
    m_layout->addWidget(ui->fillButton);
    m_layout->addStretch();

    ui->penButton->setCheckable(true);
    ui->moveButton->setCheckable(true);
    ui->eraserButton->setCheckable(true);
    ui->fillButton->setCheckable(true);
    setTool(PaintOpenGLWidget::Tool::Pen);

    connect(m_arrowButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Arrow);
        emit toolSelected(PaintOpenGLWidget::Tool::Arrow);
    });

    connect(ui->penButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Pen);
        emit toolSelected(PaintOpenGLWidget::Tool::Pen);
    });
    connect(ui->moveButton, &QPushButton::clicked, this, [this]() {
        setTool(PaintOpenGLWidget::Tool::Move);
        emit toolSelected(PaintOpenGLWidget::Tool::Move);
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
    const QSignalBlocker penBlocker(ui->penButton);
    const QSignalBlocker moveBlocker(ui->moveButton);
    const QSignalBlocker eraserBlocker(ui->eraserButton);
    const QSignalBlocker fillBlocker(ui->fillButton);

    m_arrowButton->setChecked(tool == PaintOpenGLWidget::Tool::Arrow);
    ui->penButton->setChecked(tool == PaintOpenGLWidget::Tool::Pen);
    ui->moveButton->setChecked(tool == PaintOpenGLWidget::Tool::Move);
    ui->eraserButton->setChecked(tool == PaintOpenGLWidget::Tool::Eraser ||
                                 tool == PaintOpenGLWidget::Tool::DeleteLine);
    ui->fillButton->setChecked(tool == PaintOpenGLWidget::Tool::Fill);
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
            const QSignalBlocker penBlocker(ui->penButton);
            const QSignalBlocker moveBlocker(ui->moveButton);
            const QSignalBlocker eraserBlocker(ui->eraserButton);
            const QSignalBlocker fillBlocker(ui->fillButton);
            m_arrowButton->setChecked(false);
            ui->penButton->setChecked(false);
            ui->moveButton->setChecked(false);
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
