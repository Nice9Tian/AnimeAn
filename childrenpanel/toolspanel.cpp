#include "toolspanel.h"
#include "ui_toolspanel.h"

#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

ToolsPanel::ToolsPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolsPanel)
{
    ui->setupUi(this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(ui->penButton);
    layout->addWidget(ui->moveButton);
    layout->addWidget(ui->eraserButton);
    layout->addWidget(ui->fillButton);
    layout->addStretch();

    ui->penButton->setCheckable(true);
    ui->moveButton->setCheckable(true);
    ui->eraserButton->setCheckable(true);
    ui->fillButton->setCheckable(true);
    setTool(PaintOpenGLWidget::Tool::Pen);

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
    const QSignalBlocker penBlocker(ui->penButton);
    const QSignalBlocker moveBlocker(ui->moveButton);
    const QSignalBlocker eraserBlocker(ui->eraserButton);
    const QSignalBlocker fillBlocker(ui->fillButton);

    ui->penButton->setChecked(tool == PaintOpenGLWidget::Tool::Pen);
    ui->moveButton->setChecked(tool == PaintOpenGLWidget::Tool::Move);
    ui->eraserButton->setChecked(tool == PaintOpenGLWidget::Tool::Eraser ||
                                 tool == PaintOpenGLWidget::Tool::DeleteLine);
    ui->fillButton->setChecked(tool == PaintOpenGLWidget::Tool::Fill);
}
