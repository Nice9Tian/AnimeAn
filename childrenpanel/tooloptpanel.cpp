#include "tooloptpanel.h"
#include "ui_tooloptpanel.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLayoutItem>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace {
QString textValue(const QJsonObject &object, const QString &key, const QString &fallback = QString())
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

int intValue(const QJsonObject &object, const QString &key, int fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toInt() : fallback;
}

QColor colorFromState(const QJsonObject &state)
{
    if (state.contains(QStringLiteral("color"))) {
        const QColor namedColor(state.value(QStringLiteral("color")).toString());
        if (namedColor.isValid()) {
            return namedColor;
        }
    }

    const int r = intValue(state, QStringLiteral("r"), 0);
    const int g = intValue(state, QStringLiteral("g"), 0);
    const int b = intValue(state, QStringLiteral("b"), 0);
    const int a = intValue(state, QStringLiteral("a"), 255);
    return QColor(r, g, b, a);
}

QString colorButtonStyle(const QString &standardStyle, const QColor &color)
{
    const QString textColor = color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black");
    const QColor hoverColor = color.lightness() < 128 ? color.lighter(135) : color.darker(108);
    const QColor pressedColor = color.lightness() < 128 ? color.lighter(165) : color.darker(125);

    return standardStyle + QStringLiteral(
               "QPushButton {"
               " background-color: %1;"
               " color: %2;"
               "}"
               "QPushButton:hover {"
               " background-color: %3;"
               "}"
               "QPushButton:pressed {"
               " background-color: %4;"
               "}")
        .arg(color.name(QColor::HexRgb),
             textColor,
             hoverColor.name(QColor::HexRgb),
             pressedColor.name(QColor::HexRgb));
}
}

ToolOptPanel::ToolOptPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolOptPanel)
{
    ui->setupUi(this);
    ui->standardButton->hide();
    ui->standardSlider->hide();
    ui->standardList->hide();

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(8);
    m_layout->addStretch();
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
}

void ToolOptPanel::configureControls(const QJsonArray &controls)
{
    clearControls();

    for (const QJsonValue &value : controls) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject control = value.toObject();
        QWidget *widget = nullptr;
        const QString type = textValue(control, QStringLiteral("type"));
        if (type == QStringLiteral("button_row")) {
            widget = createButtonRow(control);
        } else if (type == QStringLiteral("list")) {
            widget = createListControl(control);
        } else if (type == QStringLiteral("slider")) {
            widget = createSliderControl(control);
        }

        if (!widget) {
            continue;
        }

        const QString name = textValue(control, QStringLiteral("name"));
        if (!name.isEmpty()) {
            m_controls.insert(name, widget);
        }
        m_layout->insertWidget(m_layout->count() - 1, widget);
    }
}

void ToolOptPanel::setFillScope(PaintOpenGLWidget::FillScope scope)
{
    QListWidget *list = findChild<QListWidget *>(QStringLiteral("fill_scope"));
    if (!list) {
        return;
    }

    const QSignalBlocker blocker(list);
    list->setCurrentRow(scope == PaintOpenGLWidget::FillScope::AllLayers ? 0 : 1);
}

void ToolOptPanel::setSmoothValue(int value)
{
    QSlider *slider = findChild<QSlider *>(QStringLiteral("smooth"));
    QLabel *label = findChild<QLabel *>(QStringLiteral("smooth_label"));
    if (!slider) {
        return;
    }

    const QSignalBlocker blocker(slider);
    slider->setValue(value);
    if (label) {
        label->setText(QStringLiteral("Smooth: %1").arg(value));
    }
}

QWidget *ToolOptPanel::createButtonRow(const QJsonObject &control)
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *outerLayout = new QVBoxLayout(container);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(4);

    const QString title = textValue(control, QStringLiteral("title"));
    if (!title.isEmpty()) {
        QLabel *label = new QLabel(title, container);
        label->setObjectName(textValue(control, QStringLiteral("name")) + QStringLiteral("_label"));
        outerLayout->addWidget(label);
    }

    QWidget *row = new QWidget(container);
    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(6);
    outerLayout->addWidget(row);

    const QString name = textValue(control, QStringLiteral("name"));
    const QString hook = textValue(control, QStringLiteral("hook"), name);
    const QJsonArray options = control.value(QStringLiteral("options")).toArray();
    for (const QJsonValue &optionValue : options) {
        if (!optionValue.isObject()) {
            continue;
        }

        const QJsonObject option = optionValue.toObject();
        QPushButton *button = new QPushButton(textValue(option, QStringLiteral("title")), row);
        button->setCursor(ui->standardButton->cursor());
        button->setMinimumSize(ui->standardButton->minimumSize());
        button->setStyleSheet(ui->standardButton->styleSheet());
        const QString value = textValue(option, QStringLiteral("value"));
        const QJsonObject state = option.value(QStringLiteral("state")).toObject();
        const QColor color = colorFromState(state);
        if (color.isValid()) {
            button->setStyleSheet(colorButtonStyle(ui->standardButton->styleSheet(), color));
        }
        connect(button, &QPushButton::clicked, this, [this, hook, name, value]() {
            emitOptionChanged(hook, name, value);
        });
        rowLayout->addWidget(button);
    }
    rowLayout->addStretch();

    return container;
}

QWidget *ToolOptPanel::createListControl(const QJsonObject &control)
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    const QString name = textValue(control, QStringLiteral("name"));
    const QString title = textValue(control, QStringLiteral("title"));
    QLabel *label = new QLabel(title, container);
    label->setObjectName(name + QStringLiteral("_label"));
    layout->addWidget(label);

    QListWidget *list = new QListWidget(container);
    list->setObjectName(name);
    list->setStyleSheet(ui->standardList->styleSheet());
    list->setFont(ui->standardList->font());
    list->setFrameShape(ui->standardList->frameShape());
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMaximumHeight(intValue(control, QStringLiteral("height"), 62));
    const QString current = textValue(control, QStringLiteral("value"));

    const QJsonArray options = control.value(QStringLiteral("options")).toArray();
    int selectedRow = 0;
    for (int row = 0; row < options.size(); ++row) {
        const QJsonValue optionValue = options.at(row);
        if (!optionValue.isObject()) {
            continue;
        }

        const QJsonObject option = optionValue.toObject();
        QListWidgetItem *item = new QListWidgetItem(textValue(option, QStringLiteral("title")), list);
        const QString value = textValue(option, QStringLiteral("value"));
        item->setData(Qt::UserRole, value);
        if (value == current) {
            selectedRow = row;
        }
    }
    list->setCurrentRow(selectedRow);

    const QString hook = textValue(control, QStringLiteral("hook"), name);
    connect(list, &QListWidget::currentRowChanged, this, [this, list, hook, name](int row) {
        QListWidgetItem *item = list->item(row);
        if (item) {
            emitOptionChanged(hook, name, item->data(Qt::UserRole));
        }
    });

    layout->addWidget(list);
    return container;
}

QWidget *ToolOptPanel::createSliderControl(const QJsonObject &control)
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    const QString name = textValue(control, QStringLiteral("name"));
    const QString title = textValue(control, QStringLiteral("title"), name);
    const int value = intValue(control, QStringLiteral("value"), 0);
    QLabel *label = new QLabel(QStringLiteral("%1: %2").arg(title).arg(value), container);
    label->setObjectName(name + QStringLiteral("_label"));
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    QSlider *slider = new QSlider(Qt::Horizontal, container);
    slider->setObjectName(name);
    slider->setCursor(ui->standardSlider->cursor());
    slider->setStyleSheet(ui->standardSlider->styleSheet());
    slider->setSingleStep(ui->standardSlider->singleStep());
    slider->setPageStep(ui->standardSlider->pageStep());
    slider->setTracking(ui->standardSlider->hasTracking());
    slider->setRange(intValue(control, QStringLiteral("min"), 0), intValue(control, QStringLiteral("max"), 100));
    slider->setValue(value);

    const QString hook = textValue(control, QStringLiteral("hook"), name);
    connect(slider, &QSlider::valueChanged, this, [this, label, hook, name, title](int newValue) {
        label->setText(QStringLiteral("%1: %2").arg(title).arg(newValue));
        emitOptionChanged(hook, name, newValue);
    });

    layout->addWidget(slider);
    return container;
}

void ToolOptPanel::emitOptionChanged(const QString &hook, const QString &name, const QVariant &value)
{
    emit optionChanged(hook, name, value);

    if (hook == QStringLiteral("color")) {
        const QColor color(value.toString());
        if (color.isValid()) {
            emit colorSelected(color);
        }
    } else if (hook == QStringLiteral("fill_scope")) {
        emit fillScopeSelected(value.toString() == QStringLiteral("all")
                                   ? PaintOpenGLWidget::FillScope::AllLayers
                                   : PaintOpenGLWidget::FillScope::CurrentLayer);
    } else if (hook == QStringLiteral("eraser_mode")) {
        const PaintOpenGLWidget::Tool eraserTool = value.toString() == QStringLiteral("line")
                                                       ? PaintOpenGLWidget::Tool::DeleteLine
                                                       : PaintOpenGLWidget::Tool::Eraser;
        setTool(eraserTool);
        emit eraserModeSelected(eraserTool);
    } else if (hook == QStringLiteral("smooth")) {
        emit smoothValueChanged(value.toInt());
    } else if (hook == QStringLiteral("pen_width")) {
        emit penWidthChanged(value.toInt());
    }
}

void ToolOptPanel::clearControls()
{
    m_controls.clear();
    while (m_layout->count() > 1) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}
