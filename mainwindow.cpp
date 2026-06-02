#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "paintopenglwidget.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_paintWidget = new PaintOpenGLWidget(ui->centralwidget);
    m_paintWidget->setObjectName(ui->graphicsView->objectName());
    m_paintWidget->setGeometry(ui->graphicsView->geometry());
    delete ui->graphicsView;
    ui->graphicsView = m_paintWidget;

    connect(ui->blueButton, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setPenColor(Qt::blue);
        ui->Pen->setChecked(true);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(false);
    });

    connect(ui->greenButton, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setPenColor(Qt::green);
        ui->Pen->setChecked(true);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(false);
    });

    ui->Pen->setCheckable(true);
    ui->Eraser->setCheckable(true);
    ui->LineErazer->setCheckable(true);
    ui->Pen->setChecked(true);
    ui->SmoothValue->setRange(0, 100);
    ui->SmoothValue->setValue(50);
    ui->SmoothValue_print->setText(QStringLiteral("Smooth: 50"));
    m_paintWidget->setSmoothValue(ui->SmoothValue->value());

    connect(ui->Pen, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setTool(PaintOpenGLWidget::Tool::Pen);
        ui->Pen->setChecked(true);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(false);
    });

    connect(ui->Eraser, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setTool(PaintOpenGLWidget::Tool::Eraser);
        ui->Pen->setChecked(false);
        ui->Eraser->setChecked(true);
        ui->LineErazer->setChecked(false);
    });

    connect(ui->LineErazer, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setTool(PaintOpenGLWidget::Tool::DeleteLine);
        ui->Pen->setChecked(false);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(true);
    });

    connect(ui->SmoothValue, &QSlider::valueChanged, this, [this](int value) {
        m_paintWidget->setSmoothValue(value);
        ui->SmoothValue_print->setText(QStringLiteral("Smooth: %1").arg(value));
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}
