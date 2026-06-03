#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "paintopenglwidget.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>

#ifdef ANIMEAN_WITH_PYTHON
#ifdef slots
#undef slots
#define ANIMEAN_RESTORE_QT_SLOTS
#endif
#include <pybind11/embed.h>
#ifdef ANIMEAN_RESTORE_QT_SLOTS
#define slots Q_SLOTS
#undef ANIMEAN_RESTORE_QT_SLOTS
#endif

#include <string>

namespace py = pybind11;
#endif

namespace {
int movedRowTarget(int sourceRow, int destinationChild)
{
    int target = destinationChild;
    if (sourceRow < destinationChild) {
        --target;
    }
    return target;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_paintWidget = ui->graphicsView;

    ui->LayerList->setDragDropMode(QAbstractItemView::InternalMove);
    ui->LayerList->setDefaultDropAction(Qt::MoveAction);
    ui->LayerList->setDragDropOverwriteMode(false);
    ui->LayerList->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->FrameList->setDragDropMode(QAbstractItemView::InternalMove);
    ui->FrameList->setDefaultDropAction(Qt::MoveAction);
    ui->FrameList->setDragDropOverwriteMode(false);
    ui->FrameList->setSelectionMode(QAbstractItemView::SingleSelection);

    refreshLayerList(0);
    refreshFrameList(0);

    connect(ui->blueButton, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setPenColor(Qt::blue);
        ui->Pen->setChecked(true);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(false);
        ui->Fill->setChecked(false);
    });

    connect(ui->greenButton, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setPenColor(Qt::green);
        ui->Pen->setChecked(true);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(false);
        ui->Fill->setChecked(false);
    });

    ui->Pen->setCheckable(true);
    ui->Eraser->setCheckable(true);
    ui->LineErazer->setCheckable(true);
    ui->Fill->setCheckable(true);
    ui->Pen->setChecked(true);
    ui->FillOptArea->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->FillOptArea->setCurrentRow(0);
    ui->SmoothValue->setRange(0, 100);
    ui->SmoothValue->setValue(50);
    ui->SmoothValue_print->setText(QStringLiteral("Smooth: 50"));
    m_paintWidget->setSmoothValue(ui->SmoothValue->value());

#ifdef ANIMEAN_WITH_PYTHON
    try {
        py::list pythonPath = py::module_::import("sys").attr("path");
        pythonPath.insert(0, QCoreApplication::applicationDirPath().toStdString());
        if (QFileInfo::exists(QDir(QStringLiteral(ANIMEAN_SOURCE_DIR)).filePath(QStringLiteral("hello_world.py")))) {
            pythonPath.insert(0, ANIMEAN_SOURCE_DIR);
        }

        const std::string helloText = py::module_::import("hello_world")
                                          .attr("hello_world")()
                                          .cast<std::string>();
        ui->label->setText(QString::fromStdString(helloText));
    } catch (const py::error_already_set &error) {
        ui->label->setText(QStringLiteral("Python error: %1").arg(QString::fromUtf8(error.what())));
    }
#else
    ui->label->setText(QStringLiteral("Python disabled"));
#endif

    connect(ui->Pen, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setTool(PaintOpenGLWidget::Tool::Pen);
        ui->Pen->setChecked(true);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(false);
        ui->Fill->setChecked(false);
    });

    connect(ui->Eraser, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setTool(PaintOpenGLWidget::Tool::Eraser);
        ui->Pen->setChecked(false);
        ui->Eraser->setChecked(true);
        ui->LineErazer->setChecked(false);
        ui->Fill->setChecked(false);
    });

    connect(ui->LineErazer, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setTool(PaintOpenGLWidget::Tool::DeleteLine);
        ui->Pen->setChecked(false);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(true);
        ui->Fill->setChecked(false);
    });

    connect(ui->Fill, &QPushButton::clicked, this, [this]() {
        m_paintWidget->setTool(PaintOpenGLWidget::Tool::Fill);
        ui->Pen->setChecked(false);
        ui->Eraser->setChecked(false);
        ui->LineErazer->setChecked(false);
        ui->Fill->setChecked(true);
    });

    connect(ui->FillOptArea, &QListWidget::currentRowChanged, this, [this](int row) {
        m_paintWidget->setFillScope(row == 1
                                        ? PaintOpenGLWidget::FillScope::AllLayers
                                        : PaintOpenGLWidget::FillScope::CurrentLayer);
    });

    connect(ui->SmoothValue, &QSlider::valueChanged, this, [this](int value) {
        m_paintWidget->setSmoothValue(value);
        ui->SmoothValue_print->setText(QStringLiteral("Smooth: %1").arg(value));
    });

    connect(ui->PythonAxisButton, &QPushButton::clicked, this, [this]() {
#ifdef ANIMEAN_WITH_PYTHON
        try {
            py::module_::import("animean_python");
            const std::string result = py::module_::import("hello_world")
                                           .attr("draw_axis_test")(
                                               py::cast(&m_paintWidget->model(), py::return_value_policy::reference),
                                               m_paintWidget->width(),
                                               m_paintWidget->height())
                                           .cast<std::string>();
            m_paintWidget->update();
            ui->label->setText(QString::fromStdString(result));
        } catch (const py::error_already_set &error) {
            ui->label->setText(QStringLiteral("Python axis error: %1").arg(QString::fromUtf8(error.what())));
        }
#else
        ui->label->setText(QStringLiteral("Python disabled"));
#endif
    });

    connect(ui->CellDictButton, &QPushButton::clicked, this, [this]() {
#ifdef ANIMEAN_WITH_PYTHON
        try {
            py::module_::import("animean_python");
            const py::object model = py::cast(&m_paintWidget->model(), py::return_value_policy::reference);
            const py::object cellDict = model.attr("cell_to_dict")(
                m_paintWidget->model().currentLayer(),
                m_paintWidget->model().currentFrame(),
                false,
                4.0);
            ui->label->setText(QString::fromStdString(py::str(cellDict).cast<std::string>()));
        } catch (const py::error_already_set &error) {
            ui->label->setText(QStringLiteral("Python cell dict error: %1").arg(QString::fromUtf8(error.what())));
        }
#else
        ui->label->setText(QStringLiteral("Python disabled"));
#endif
    });

    connect(ui->LayerList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists && row >= 0) {
            m_paintWidget->setCurrentLayer(row);
        }
    });

    connect(ui->FrameList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingLists && row >= 0) {
            m_paintWidget->setCurrentFrame(row);
        }
    });

    connect(ui->AddLayerButton, &QPushButton::clicked, this, [this]() {
        refreshLayerList(m_paintWidget->addLayer());
    });

    connect(ui->DeleteLayerButton, &QPushButton::clicked, this, [this]() {
        const int row = ui->LayerList->currentRow();
        if (m_paintWidget->deleteLayer(row)) {
            refreshLayerList(row < m_paintWidget->layerCount() ? row : m_paintWidget->layerCount() - 1);
        }
    });

    connect(ui->AddFrameButton, &QPushButton::clicked, this, [this]() {
        refreshFrameList(m_paintWidget->addFrame());
    });

    connect(ui->DeleteFrameButton, &QPushButton::clicked, this, [this]() {
        const int row = ui->FrameList->currentRow();
        if (m_paintWidget->deleteFrame(row)) {
            refreshFrameList(row < m_paintWidget->frameCount() ? row : m_paintWidget->frameCount() - 1);
        }
    });

    connect(ui->LayerList->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &, int sourceStart, int sourceEnd,
                         const QModelIndex &, int destinationChild) {
        if (m_refreshingLists || sourceStart != sourceEnd) {
            return;
        }
        const int target = movedRowTarget(sourceStart, destinationChild);
        if (!m_paintWidget->moveLayer(sourceStart, target)) {
            refreshLayerList(ui->LayerList->currentRow());
            return;
        }
        refreshLayerList(target);
    });

    connect(ui->FrameList->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &, int sourceStart, int sourceEnd,
                         const QModelIndex &, int destinationChild) {
        if (m_refreshingLists || sourceStart != sourceEnd) {
            return;
        }
        const int target = movedRowTarget(sourceStart, destinationChild);
        if (!m_paintWidget->moveFrame(sourceStart, target)) {
            refreshFrameList(ui->FrameList->currentRow());
            return;
        }
        refreshFrameList(target);
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::refreshLayerList(int selectedRow)
{
    m_refreshingLists = true;
    const QSignalBlocker blocker(ui->LayerList);
    ui->LayerList->clear();
    for (int i = 0; i < m_paintWidget->layerCount(); ++i) {
        ui->LayerList->addItem(m_paintWidget->layerName(i));
    }
    if (ui->LayerList->count() > 0) {
        if (selectedRow < 0) {
            selectedRow = 0;
        } else if (selectedRow >= ui->LayerList->count()) {
            selectedRow = ui->LayerList->count() - 1;
        }
        ui->LayerList->setCurrentRow(selectedRow);
        m_paintWidget->setCurrentLayer(selectedRow);
    }
    m_refreshingLists = false;
}

void MainWindow::refreshFrameList(int selectedRow)
{
    m_refreshingLists = true;
    const QSignalBlocker blocker(ui->FrameList);
    ui->FrameList->clear();
    for (int i = 0; i < m_paintWidget->frameCount(); ++i) {
        ui->FrameList->addItem(m_paintWidget->frameName(i));
    }
    if (ui->FrameList->count() > 0) {
        if (selectedRow < 0) {
            selectedRow = 0;
        } else if (selectedRow >= ui->FrameList->count()) {
            selectedRow = ui->FrameList->count() - 1;
        }
        ui->FrameList->setCurrentRow(selectedRow);
        m_paintWidget->setCurrentFrame(selectedRow);
    }
    m_refreshingLists = false;
}
