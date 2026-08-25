#include "tooloptpanel.h"
#include "ui_tooloptpanel.h"
#include "palettecontrol.h"
#include "theme.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QGridLayout>
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
// Which prototype a generated control took its style from, so a theme change
// can rebuild that style in place instead of rebuilding the whole panel.
// Untagged widgets (the colour swatch, labels) are palette-driven and follow
// the application palette on their own.
const char kStyleRoleProperty[] = "animeanStyleRole";
// The tint a colour button was built with; its style is that tint composed
// with the current prototype style.
const char kButtonTintProperty[] = "animeanButtonTint";

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

QString buttonStyleSheet()
{
    // A tinted button appends its own background over this one
    // (colorButtonStyle); the base is what an untinted button reads as, and a
    // stylesheeted QPushButton paints no background unless it is given one.
    return QStringLiteral("QPushButton {"
                          " border: 1px solid %1;"
                          " border-radius: 3px;"
                          " padding: 4px 8px;"
                          " background: %2;"
                          "}"
                          "QPushButton:hover {"
                          " border-color: %3;"
                          "}"
                          "QPushButton:pressed {"
                          " padding-top: 5px;"
                          " padding-left: 9px;"
                          "}"
                          "QPushButton:focus {"
                          " border: 1px solid %4;"
                          "}")
        .arg(AnimeTheme::color(AnimeTheme::Role::Divider).name(),
             AnimeTheme::color(AnimeTheme::Role::SurfaceAlt).name(),
             AnimeTheme::color(AnimeTheme::Role::Accent).name(),
             AnimeTheme::color(AnimeTheme::Role::AccentHover).name());
}

QString sliderStyleSheet()
{
    // The handle is palette-driven (midlight over mid): the palette already
    // puts it a step above the groove in either mode.
    return QStringLiteral("QSlider::groove:horizontal {"
                          " height: 6px;"
                          " border-radius: 3px;"
                          " background: %1;"
                          "}"
                          "QSlider::sub-page:horizontal {"
                          " border-radius: 3px;"
                          " background: %2;"
                          "}"
                          "QSlider::handle:horizontal {"
                          " width: 14px;"
                          " height: 14px;"
                          " margin: -5px 0;"
                          " border: 1px solid palette(mid);"
                          " border-radius: 7px;"
                          " background: palette(midlight);"
                          "}"
                          "QSlider::handle:horizontal:hover {"
                          " border-color: %3;"
                          "}"
                          "QSlider::handle:horizontal:pressed {"
                          " background: %2;"
                          "}")
        .arg(AnimeTheme::color(AnimeTheme::Role::SurfaceAlt).name(),
             AnimeTheme::color(AnimeTheme::Role::Accent).name(),
             AnimeTheme::color(AnimeTheme::Role::AccentHover).name());
}

QString listStyleSheet()
{
    // Everything here except the hover wash is a palette role, so a list never
    // needs restyling; the wash needs an alpha the palette cannot express.
    const QColor accent = AnimeTheme::color(AnimeTheme::Role::Accent);
    return QStringLiteral("QListWidget {"
                          " border: 1px solid palette(mid);"
                          " border-radius: 3px;"
                          " background: palette(base);"
                          " padding: 2px;"
                          "}"
                          "QListWidget::item {"
                          " min-height: 22px;"
                          " padding: 3px 6px;"
                          " border-radius: 2px;"
                          " color: palette(text);"
                          "}"
                          "QListWidget::item:hover {"
                          " background: rgba(%1, %2, %3, 0.35);"
                          "}"
                          "QListWidget::item:selected {"
                          " color: palette(highlighted-text);"
                          " background: palette(highlight);"
                          "}")
        .arg(accent.red())
        .arg(accent.green())
        .arg(accent.blue());
}

void restyleControl(QWidget *widget, const QString &buttonStyle, const QString &sliderStyle)
{
    const QString role = widget->property(kStyleRoleProperty).toString();
    if (role == QStringLiteral("button")) {
        const QVariant tint = widget->property(kButtonTintProperty);
        widget->setStyleSheet(tint.isValid() ? colorButtonStyle(buttonStyle, tint.value<QColor>())
                                             : buttonStyle);
    } else if (role == QStringLiteral("slider")) {
        widget->setStyleSheet(sliderStyle);
    }
}
}

ToolOptPanel::ToolOptPanel(QWidget *parent, bool subControlHost)
    : QWidget(parent)
    , ui(new Ui::ToolOptPanel)
    , m_isSubControlHost(subControlHost)
{
    ui->setupUi(this);
    ui->standardButton->hide();
    ui->standardSlider->hide();
    ui->standardList->hide();
    applyTheme();
    connect(AnimeTheme::instance(), &AnimeTheme::themeChanged, this, &ToolOptPanel::applyTheme);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(8);
    m_layout->addStretch();

    if (m_isSubControlHost) {
        SubControlRegistry::instance()->registerHost(this);
    }
}

ToolOptPanel::~ToolOptPanel()
{
    // Before the layout takes its children down with it: a frame outlives
    // every panel it passes through.
    parkSubControlFrames();
    if (m_isSubControlHost) {
        SubControlRegistry::instance()->unregisterHost(this);
    }
    delete ui;
}

QWidget *ToolOptPanel::subControlHostWidget()
{
    return this;
}

QRect ToolOptPanel::subControlPreviewRect(const QPoint &globalPos) const
{
    if (!isVisible()) {
        return QRect();
    }
    const QPoint local = mapFromGlobal(globalPos);
    if (!rect().contains(local)) {
        return QRect();
    }
    // The slot is a full-width band at the foot of the option column - where
    // an appended row actually lands. Its height is what a frame needs to be
    // readable, not what is left over, so the preview means the same thing on
    // a crowded panel as on an empty one.
    const QMargins margins = m_layout ? m_layout->contentsMargins() : QMargins(8, 8, 8, 8);
    int top = margins.top();
    for (int index = 0; m_layout && index < m_layout->count(); ++index) {
        if (QWidget *widget = m_layout->itemAt(index)->widget()) {
            top = qMax(top, widget->geometry().bottom() + m_layout->spacing());
        }
    }
    const int bandHeight = qMin(qMax(80, height() / 3), qMax(40, height() - top - margins.bottom()));
    const QRect band(margins.left(), qMin(top, qMax(0, height() - bandHeight - margins.bottom())),
                     qMax(40, width() - margins.left() - margins.right()), bandHeight);
    return QRect(mapToGlobal(band.topLeft()), band.size());
}

void ToolOptPanel::embedSubControl(SubControlFrame *frame)
{
    if (!frame || !m_layout) {
        return;
    }
    frame->setParent(this);
    // Ahead of the trailing stretch, like every other generated row.
    m_layout->insertWidget(qMax(0, m_layout->count() - 1), frame);
    if (!m_subControlFrames.contains(frame)) {
        m_subControlFrames.append(frame);
    }
}

void ToolOptPanel::parkSubControlFrames()
{
    const QVector<QPointer<SubControlFrame>> frames = m_subControlFrames;
    m_subControlFrames.clear();
    for (const QPointer<SubControlFrame> &frame : frames) {
        // Only what is still ours: a frame the user has since dragged out is
        // somewhere else's problem, and parking it would take it off screen.
        if (frame && isAncestorOf(frame)) {
            frame->park();
        }
    }
}

PaintOpenGLWidget::Tool ToolOptPanel::tool() const
{
    return m_tool;
}

void ToolOptPanel::setTool(PaintOpenGLWidget::Tool tool)
{
    m_tool = tool;
}

void ToolOptPanel::configureLayout(const QJsonObject &layout)
{
    clearControls();
    configureControls(layout.value(QStringLiteral("controls")).toArray(),
                      intValue(layout, QStringLiteral("row_spacing"), 8),
                      intValue(layout, QStringLiteral("column_spacing"), 6));
}

void ToolOptPanel::configureControls(const QJsonArray &controls)
{
    clearControls();
    configureControls(controls, 8, 6);
}

void ToolOptPanel::configureControls(const QJsonArray &controls, int rowSpacing, int columnSpacing)
{
    QWidget *gridContainer = new QWidget(this);
    QGridLayout *gridLayout = new QGridLayout(gridContainer);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setHorizontalSpacing(columnSpacing);
    gridLayout->setVerticalSpacing(rowSpacing);

    QMap<int, int> nextColumnForRow;
    bool hasWidgets = false;
    for (const QJsonValue &value : controls) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject control = value.toObject();
        QWidget *widget = nullptr;
        const QString type = textValue(control, QStringLiteral("type"));

        const int row = intValue(control, QStringLiteral("row"), 0);
        const int startColumn = control.contains(QStringLiteral("start_column"))
                                    ? intValue(control, QStringLiteral("start_column"), 0)
                                    : (control.contains(QStringLiteral("column"))
                                           ? intValue(control, QStringLiteral("column"), 0)
                                           : nextColumnForRow.value(row, 0));
        const int endColumn = intValue(control, QStringLiteral("end_column"), startColumn);
        const int columnSpan = qMax(1, endColumn - startColumn + 1);
        control.insert(QStringLiteral("row"), row);
        control.insert(QStringLiteral("start_column"), startColumn);
        control.insert(QStringLiteral("end_column"), startColumn + columnSpan - 1);

        if (type == QStringLiteral("button")) {
            widget = createButtonControl(control);
        } else if (type == QStringLiteral("list")) {
            widget = createListControl(control);
        } else if (type == QStringLiteral("slider")) {
            widget = createSliderControl(control);
        } else if (type == QStringLiteral("check") || type == QStringLiteral("checkbox")) {
            widget = createCheckControl(control);
        } else if (type == QStringLiteral("color")) {
            widget = createColorControl(control);
        } else if (type == QStringLiteral("palette")) {
            widget = createPaletteControl(control);
        } else if (type == QStringLiteral("subwindow")) {
            widget = createSubWindowControl(control);
        }

        if (!widget) {
            continue;
        }

        const QString name = textValue(control, QStringLiteral("name"));
        if (!name.isEmpty()) {
            m_controls.insert(name, widget);
            m_controlValues.insert(name, control.value(QStringLiteral("value")).toVariant().toString());
        }

        const QJsonObject visibleWhen = control.value(QStringLiteral("visible_when")).toObject();
        const QString watch = textValue(visibleWhen, QStringLiteral("name"));
        if (!watch.isEmpty()) {
            VisibilityRule rule;
            rule.watch = watch;
            for (const QJsonValue &option : visibleWhen.value(QStringLiteral("values")).toArray()) {
                rule.values.append(option.toVariant().toString());
            }
            const QString single = visibleWhen.value(QStringLiteral("value")).toVariant().toString();
            if (!single.isEmpty()) {
                rule.values.append(single);
            }
            rule.target = widget;
            m_visibilityRules.append(rule);
        }

        nextColumnForRow[row] = qMax(nextColumnForRow.value(row, 0), startColumn + columnSpan);
        gridLayout->addWidget(widget, row, startColumn, 1, columnSpan);
        hasWidgets = true;
    }

    if (hasWidgets) {
        applyVisibilityRules();
        m_layout->insertWidget(m_layout->count() - 1, gridContainer);
    } else {
        gridContainer->deleteLater();
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

void ToolOptPanel::applyTheme()
{
    const QString buttonStyle = buttonStyleSheet();
    const QString sliderStyle = sliderStyleSheet();
    ui->standardButton->setStyleSheet(buttonStyle);
    ui->standardSlider->setStyleSheet(sliderStyle);
    ui->standardList->setStyleSheet(listStyleSheet());

    for (QWidget *control : findChildren<QWidget *>()) {
        restyleControl(control, buttonStyle, sliderStyle);
    }
}

QWidget *ToolOptPanel::createButtonControl(const QJsonObject &control)
{
    const QString name = textValue(control, QStringLiteral("name"));
    const QString hook = textValue(control, QStringLiteral("hook"), name);
    const int row = intValue(control, QStringLiteral("row"), 0);
    const int startColumn = intValue(control, QStringLiteral("start_column"), 0);
    const int endColumn = intValue(control, QStringLiteral("end_column"), startColumn);
    QPushButton *button = new QPushButton(textValue(control, QStringLiteral("title")), this);
    button->setCursor(ui->standardButton->cursor());
    button->setMinimumSize(ui->standardButton->minimumSize());
    button->setProperty(kStyleRoleProperty, QStringLiteral("button"));
    button->setStyleSheet(ui->standardButton->styleSheet());
    const QString value = textValue(control, QStringLiteral("value"));
    const QJsonObject state = control.value(QStringLiteral("state")).toObject();
    const QColor color = colorFromState(state);
    if (color.isValid()) {
        button->setProperty(kButtonTintProperty, color);
        button->setStyleSheet(colorButtonStyle(ui->standardButton->styleSheet(), color));
    }
    connect(button, &QPushButton::clicked, this, [this, hook, name, value, row, startColumn, endColumn]() {
        emitOptionChanged(hook, name, QStringLiteral("button"), value, row, startColumn, endColumn);
    });
    return button;
}

QWidget *ToolOptPanel::createListControl(const QJsonObject &control)
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    const QString name = textValue(control, QStringLiteral("name"));
    const QString type = textValue(control, QStringLiteral("type"), QStringLiteral("list"));
    const int controlRow = intValue(control, QStringLiteral("row"), 0);
    const int startColumn = intValue(control, QStringLiteral("start_column"), 0);
    const int endColumn = intValue(control, QStringLiteral("end_column"), startColumn);
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
    const QString current = textValue(control, QStringLiteral("value"));

    const QJsonArray options = control.value(QStringLiteral("options")).toArray();
    list->setMaximumHeight(qMax(62, options.size() * 28 + 8));
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
    connect(list, &QListWidget::currentRowChanged, this, [this, list, hook, name, type, controlRow, startColumn, endColumn](int itemRow) {
        QListWidgetItem *item = list->item(itemRow);
        if (item) {
            emitOptionChanged(hook, name, type, item->data(Qt::UserRole), controlRow, startColumn, endColumn);
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
    const QString type = textValue(control, QStringLiteral("type"), QStringLiteral("slider"));
    const int row = intValue(control, QStringLiteral("row"), 0);
    const int startColumn = intValue(control, QStringLiteral("start_column"), 0);
    const int endColumn = intValue(control, QStringLiteral("end_column"), startColumn);
    const QString title = textValue(control, QStringLiteral("title"), name);
    const int value = intValue(control, QStringLiteral("value"), 0);
    QLabel *label = new QLabel(QStringLiteral("%1: %2").arg(title).arg(value), container);
    label->setObjectName(name + QStringLiteral("_label"));
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    QSlider *slider = new QSlider(Qt::Horizontal, container);
    slider->setObjectName(name);
    slider->setCursor(ui->standardSlider->cursor());
    slider->setProperty(kStyleRoleProperty, QStringLiteral("slider"));
    slider->setStyleSheet(ui->standardSlider->styleSheet());
    slider->setSingleStep(ui->standardSlider->singleStep());
    slider->setPageStep(ui->standardSlider->pageStep());
    slider->setTracking(ui->standardSlider->hasTracking());
    slider->setRange(intValue(control, QStringLiteral("min"), 0), intValue(control, QStringLiteral("max"), 100));
    slider->setValue(value);

    const QString hook = textValue(control, QStringLiteral("hook"), name);
    connect(slider, &QSlider::valueChanged, this, [this, label, hook, name, type, title, row, startColumn, endColumn](int newValue) {
        label->setText(QStringLiteral("%1: %2").arg(title).arg(newValue));
        emitOptionChanged(hook, name, type, newValue, row, startColumn, endColumn);
    });

    layout->addWidget(slider);
    return container;
}

QWidget *ToolOptPanel::createCheckControl(const QJsonObject &control)
{
    const QString name = textValue(control, QStringLiteral("name"));
    const QString hook = textValue(control, QStringLiteral("hook"), name);
    const int row = intValue(control, QStringLiteral("row"), 0);
    const int startColumn = intValue(control, QStringLiteral("start_column"), 0);
    const int endColumn = intValue(control, QStringLiteral("end_column"), startColumn);
    QCheckBox *check = new QCheckBox(textValue(control, QStringLiteral("title")), this);
    check->setObjectName(name);
    check->setFont(ui->standardList->font());
    const QString value = textValue(control, QStringLiteral("value")).toLower();
    check->setChecked(value == QStringLiteral("on") || value == QStringLiteral("true")
                      || control.value(QStringLiteral("value")).toBool(false));
    connect(check, &QCheckBox::toggled, this, [this, hook, name, row, startColumn, endColumn](bool checked) {
        emitOptionChanged(hook, name, QStringLiteral("check"),
                          checked ? QStringLiteral("on") : QStringLiteral("off"),
                          row, startColumn, endColumn);
    });
    return check;
}

QWidget *ToolOptPanel::createColorControl(const QJsonObject &control)
{
    const QString name = textValue(control, QStringLiteral("name"));
    const QString hook = textValue(control, QStringLiteral("hook"), name);
    const int row = intValue(control, QStringLiteral("row"), 0);
    const int startColumn = intValue(control, QStringLiteral("start_column"), 0);
    const int endColumn = intValue(control, QStringLiteral("end_column"), startColumn);

    QWidget *container = new QWidget(this);
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    const QString title = textValue(control, QStringLiteral("title"));
    if (!title.isEmpty()) {
        QLabel *label = new QLabel(title, container);
        label->setFont(ui->standardList->font());
        layout->addWidget(label);
    }

    QPushButton *swatch = new QPushButton(container);
    swatch->setObjectName(name);
    swatch->setFlat(false);
    swatch->setFixedWidth(52);
    // The alpha is carried in the value and preserved on edit, but the swatch
    // paints it opaque: a translucent chip on a themed button reads as a
    // different colour than the one it stands for.
    QColor current(textValue(control, QStringLiteral("value")));
    if (!current.isValid()) {
        current = QColor(0, 0, 0, 255);
    }
    const auto paintSwatch = [swatch](const QColor &color) {
        QColor opaque = color;
        opaque.setAlpha(255);
        swatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid palette(mid);")
                                  .arg(opaque.name(QColor::HexRgb)));
        swatch->setToolTip(color.name(QColor::HexArgb));
    };
    paintSwatch(current);

    QColor *held = new QColor(current);
    swatch->connect(swatch, &QPushButton::clicked, this,
                    [this, swatch, held, paintSwatch, hook, name, row, startColumn, endColumn]() {
        QColorDialog dialog(*held, this);
        dialog.setOption(QColorDialog::ShowAlphaChannel, true);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QColor chosen = dialog.currentColor();
        if (!chosen.isValid()) {
            return;
        }
        *held = chosen;
        paintSwatch(chosen);
        emitOptionChanged(hook, name, QStringLiteral("color"),
                          chosen.name(QColor::HexArgb), row, startColumn, endColumn);
    });
    swatch->connect(swatch, &QObject::destroyed, [held]() { delete held; });

    layout->addWidget(swatch);
    layout->addStretch(1);
    return container;
}

QWidget *ToolOptPanel::createPaletteControl(const QJsonObject &control)
{
    const QString name = textValue(control, QStringLiteral("name"));
    const QString hook = textValue(control, QStringLiteral("hook"), name);
    const int row = intValue(control, QStringLiteral("row"), 0);
    const int startColumn = intValue(control, QStringLiteral("start_column"), 0);
    const int endColumn = intValue(control, QStringLiteral("end_column"), startColumn);

    PaletteControl *palette = new PaletteControl(control, this);
    palette->setObjectName(name);
    connect(palette, &PaletteControl::valueCommitted, this,
            [this, hook, name, row, startColumn, endColumn](const QString &argb) {
        emitOptionChanged(hook, name, QStringLiteral("palette"), argb, row, startColumn, endColumn);
    });
    // The saved set travels on its own hook whatever the value hook is: which
    // colours EXIST is a policy question, while the value hook ("color") is
    // the one C++ answers itself by arming a tool and painting with it.
    connect(palette, &PaletteControl::boxEdited, this,
            [this, name, row, startColumn, endColumn](const QString &action) {
        emitOptionChanged(QStringLiteral("palette_box"), name, QStringLiteral("palette"), action,
                          row, startColumn, endColumn);
    });
    return palette;
}

QWidget *ToolOptPanel::createSubWindowControl(const QJsonObject &control)
{
    const QString name = textValue(control, QStringLiteral("name"));
    const QString title = textValue(control, QStringLiteral("title"), name);
    SubControlFrame *frame = SubControlRegistry::instance()->frame(name);
    if (!frame) {
        // A layout naming a frame the shell does not have is a bug in the
        // layout, not a reason to lose the rest of the panel.
        QLabel *missing = new QLabel(QStringLiteral("%1 is unavailable").arg(title), this);
        missing->setObjectName(name);
        missing->setFont(ui->standardList->font());
        return missing;
    }

    if (frame->isFloating()) {
        // Its position is the user's. One thin line says where it went.
        QLabel *note = new QLabel(QStringLiteral("%1 is floating — drag it back").arg(title), this);
        note->setObjectName(name);
        note->setFont(ui->standardList->font());
        note->setStyleSheet(
            QStringLiteral("color: %1;").arg(AnimeTheme::color(AnimeTheme::Role::TextDim).name()));
        return note;
    }

    frame->setParent(this);
    // The caller puts it in the declared grid slot; this only tells the frame
    // it now lives here, so a drag out of the panel knows where it came from.
    frame->adoptedBy(this);
    frame->show();
    if (!m_subControlFrames.contains(frame)) {
        m_subControlFrames.append(frame);
    }
    return frame;
}

void ToolOptPanel::applyVisibilityRules()
{
    for (const VisibilityRule &rule : m_visibilityRules) {
        if (rule.target) {
            rule.target->setVisible(rule.values.contains(m_controlValues.value(rule.watch)));
        }
    }
}

void ToolOptPanel::emitOptionChanged(const QString &hook, const QString &name, const QString &type, const QVariant &value, int row, int startColumn, int endColumn)
{
    if (!name.isEmpty()) {
        m_controlValues.insert(name, value.toString());
        applyVisibilityRules();
    }

    emit optionChanged(hook, name, type, value, row, startColumn, endColumn);

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
        const QString mode = value.toString();
        const PaintOpenGLWidget::Tool eraserTool =
            mode == QStringLiteral("line") ? PaintOpenGLWidget::Tool::DeleteLine
            : mode == QStringLiteral("cut") ? PaintOpenGLWidget::Tool::CutLine
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
    // Before anything is deleted: a sub-control frame is the registry's, and
    // deleteLater on the container it happens to sit in would destroy it.
    parkSubControlFrames();
    m_controls.clear();
    m_visibilityRules.clear();
    m_controlValues.clear();
    while (m_layout->count() > 1) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}
