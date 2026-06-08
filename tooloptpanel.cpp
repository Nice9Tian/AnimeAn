#include "tooloptpanel.h"
#include "./ui_tooloptpanel.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

ToolOptPanel::ToolOptPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolOptPanel)
{
    ui->setupUi(this);

    m_colorRow = new QWidget(this);
    QHBoxLayout *colorLayout = new QHBoxLayout(m_colorRow);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->addWidget(ui->blackButton);
    colorLayout->addWidget(ui->blueButton);
    colorLayout->addWidget(ui->greenButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(ui->colorLabel);
    layout->addWidget(m_colorRow);
    layout->addWidget(ui->fillScopeLabel);
    layout->addWidget(ui->fillScopeList);
    layout->addWidget(ui->eraserModeLabel);
    layout->addWidget(ui->eraserModeList);
    layout->addWidget(ui->smoothLabel);
    layout->addWidget(ui->smoothSlider);
    layout->addWidget(ui->widthLabel);
    layout->addWidget(ui->widthSlider);
    layout->addStretch();

    ui->fillScopeList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->fillScopeList->setCurrentRow(1);
    ui->eraserModeList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->eraserModeList->setCurrentRow(1);

    ui->smoothSlider->setRange(0, 100);
    ui->smoothSlider->setValue(50);
    ui->smoothLabel->setText(QStringLiteral("Smooth: 50"));
    ui->widthSlider->setRange(1, 50);
    ui->widthSlider->setValue(5);
    ui->widthLabel->setText(QStringLiteral("Width: 5"));

    setColorButtonStyle();
    updateVisibleControls();

    connect(ui->blackButton, &QPushButton::clicked, this, [this]() {
        emit colorSelected(Qt::black);
    });
    connect(ui->blueButton, &QPushButton::clicked, this, [this]() {
        emit colorSelected(Qt::blue);
    });
    connect(ui->greenButton, &QPushButton::clicked, this, [this]() {
        emit colorSelected(Qt::green);
    });

    connect(ui->fillScopeList, &QListWidget::currentRowChanged, this, [this](int row) {
        emit fillScopeSelected(row == 0
                                   ? PaintOpenGLWidget::FillScope::AllLayers
                                   : PaintOpenGLWidget::FillScope::CurrentLayer);
    });

    connect(ui->eraserModeList, &QListWidget::currentRowChanged, this, [this](int row) {
        const PaintOpenGLWidget::Tool eraserTool = row == 0
                                                       ? PaintOpenGLWidget::Tool::DeleteLine
                                                       : PaintOpenGLWidget::Tool::Eraser;
        setTool(eraserTool);
        emit eraserModeSelected(eraserTool);
    });

    connect(ui->smoothSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->smoothLabel->setText(QStringLiteral("Smooth: %1").arg(value));
        emit smoothValueChanged(value);
    });

    connect(ui->widthSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->widthLabel->setText(QStringLiteral("Width: %1").arg(value));
        emit penWidthChanged(value);
    });
}

ToolOptPanel::~ToolOptPanel()
{
    delete ui;
}

PaintOpenGLWidget::Tool ToolOptPanel::tool() const
{
    return m_tool;
}

void ToolOptPanel::setTool(PaintOpenGLWidget::Tool tool)
{
    m_tool = tool;
    if (tool == PaintOpenGLWidget::Tool::Eraser || tool == PaintOpenGLWidget::Tool::DeleteLine) {
        const QSignalBlocker blocker(ui->eraserModeList);
        ui->eraserModeList->setCurrentRow(tool == PaintOpenGLWidget::Tool::DeleteLine ? 0 : 1);
    }
    updateVisibleControls();
}

void ToolOptPanel::setFillScope(PaintOpenGLWidget::FillScope scope)
{
    const QSignalBlocker blocker(ui->fillScopeList);
    ui->fillScopeList->setCurrentRow(scope == PaintOpenGLWidget::FillScope::AllLayers ? 0 : 1);
}

void ToolOptPanel::setSmoothValue(int value)
{
    const QSignalBlocker blocker(ui->smoothSlider);
    ui->smoothSlider->setValue(value);
    ui->smoothLabel->setText(QStringLiteral("Smooth: %1").arg(value));
}

void ToolOptPanel::setColorButtonStyle()
{
    ui->blackButton->setStyleSheet(QStringLiteral("QPushButton { background-color: black; color: white; }"));
    ui->blueButton->setStyleSheet(QStringLiteral("QPushButton { background-color: blue; color: white; }"));
    ui->greenButton->setStyleSheet(QStringLiteral("QPushButton { background-color: green; color: white; }"));
}

void ToolOptPanel::setControlVisible(QWidget *widget, bool visible)
{
    if (widget) {
        widget->setVisible(visible);
    }
}

void ToolOptPanel::updateVisibleControls()
{
    const bool pen = m_tool == PaintOpenGLWidget::Tool::Pen;
    const bool fill = m_tool == PaintOpenGLWidget::Tool::Fill;
    const bool eraser = m_tool == PaintOpenGLWidget::Tool::Eraser ||
                        m_tool == PaintOpenGLWidget::Tool::DeleteLine;

    setControlVisible(ui->colorLabel, pen || fill);
    setControlVisible(m_colorRow, pen || fill);
    setControlVisible(ui->fillScopeLabel, fill);
    setControlVisible(ui->fillScopeList, fill);
    setControlVisible(ui->eraserModeLabel, eraser);
    setControlVisible(ui->eraserModeList, eraser);
    setControlVisible(ui->smoothLabel, pen);
    setControlVisible(ui->smoothSlider, pen);
    setControlVisible(ui->widthLabel, pen);
    setControlVisible(ui->widthSlider, pen);
}
