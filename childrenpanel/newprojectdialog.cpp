#include "newprojectdialog.h"

#include "algorithm/animemodel.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
struct Preset {
    const char *title;
    int width;
    int height;
};

// 1280x720 first: it is the default, so it is what Enter picks.
const Preset kPresets[] = {
    {"720p  -  1280 x 720", 1280, 720},
    {"1080p  -  1920 x 1080", 1920, 1080},
    {"4K UHD  -  3840 x 2160", 3840, 2160},
    {"Square  -  1080 x 1080", 1080, 1080},
    {"Vertical  -  1080 x 1920", 1080, 1920},
    {"A4 at 150dpi  -  1240 x 1754", 1240, 1754},
};
constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));
}

NewProjectDialog::NewProjectDialog(const QSize &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("New Project"));
    setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *caption = new QLabel(
        QStringLiteral("Canvas size for the new document, in pixels."), this);
    caption->setWordWrap(true);
    layout->addWidget(caption);

    QFormLayout *form = new QFormLayout;

    m_presetBox = new QComboBox(this);
    for (int i = 0; i < kPresetCount; ++i) {
        m_presetBox->addItem(QString::fromUtf8(kPresets[i].title));
    }
    m_presetBox->addItem(QStringLiteral("Custom"));
    form->addRow(QStringLiteral("Preset"), m_presetBox);

    const QSize start = initial.isValid() && initial.width() > 0 && initial.height() > 0
                            ? initial
                            : AnimeSceneModel::defaultCanvasSize();

    m_widthBox = new QSpinBox(this);
    m_widthBox->setRange(16, 16384);
    m_widthBox->setSuffix(QStringLiteral(" px"));
    m_widthBox->setValue(start.width());
    form->addRow(QStringLiteral("Width"), m_widthBox);

    m_heightBox = new QSpinBox(this);
    m_heightBox->setRange(16, 16384);
    m_heightBox->setSuffix(QStringLiteral(" px"));
    m_heightBox->setValue(start.height());
    form->addRow(QStringLiteral("Height"), m_heightBox);

    layout->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_presetBox, &QComboBox::currentIndexChanged, this, &NewProjectDialog::applyPreset);
    connect(m_widthBox, &QSpinBox::valueChanged, this, [this](int) { syncPresetToSize(); });
    connect(m_heightBox, &QSpinBox::valueChanged, this, [this](int) { syncPresetToSize(); });

    syncPresetToSize();
    m_widthBox->setFocus();
    m_widthBox->selectAll();
}

QSize NewProjectDialog::canvasSize() const
{
    return QSize(m_widthBox->value(), m_heightBox->value());
}

void NewProjectDialog::applyPreset(int index)
{
    // Guarded both ways: choosing a preset writes the boxes, and writing the
    // boxes re-picks the preset, so without this they would chase each other.
    if (m_syncing || index < 0 || index >= kPresetCount) {
        return;
    }
    m_syncing = true;
    m_widthBox->setValue(kPresets[index].width);
    m_heightBox->setValue(kPresets[index].height);
    m_syncing = false;
}

void NewProjectDialog::syncPresetToSize()
{
    if (m_syncing) {
        return;
    }
    m_syncing = true;
    int match = kPresetCount; // the "Custom" row
    for (int i = 0; i < kPresetCount; ++i) {
        if (kPresets[i].width == m_widthBox->value()
            && kPresets[i].height == m_heightBox->value()) {
            match = i;
            break;
        }
    }
    m_presetBox->setCurrentIndex(match);
    m_syncing = false;
}
