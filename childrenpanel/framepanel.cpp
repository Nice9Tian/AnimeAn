#include "framepanel.h"
#include "ui_framepanel.h"

#include <QComboBox>
#include <QIntValidator>
#include <QRegularExpression>

namespace {
// Shooting cadences. "1s with N" reads as one drawing held for N frames of a
// 24 fps base, so the rate is 24/N.
constexpr int kBaseFps = 24;
struct Cadence {
    const char *title;
    int fps;
};
const Cadence kCadences[] = {
    {"1s with 4  (6 fps)", 6},
    {"1s with 3  (8 fps)", 8},
    {"1s with 2  (12 fps)", 12},
    {"1s with 1  (24 fps)", 24},
};
constexpr int kCadenceCount = int(sizeof(kCadences) / sizeof(kCadences[0]));
}

FramePanel::FramePanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FramePanel)
{
    ui->setupUi(this);

    for (int i = 0; i < kCadenceCount; ++i) {
        ui->FpsCombo->addItem(QString::fromUtf8(kCadences[i].title), kCadences[i].fps);
    }
    // Editable with a validator rather than a spin box: the presets are the
    // common answers, and a typed number is still just an fps.
    ui->FpsCombo->setValidator(new QIntValidator(1, 120, ui->FpsCombo));
    ui->FpsCombo->setInsertPolicy(QComboBox::NoInsert);
}

FramePanel::~FramePanel()
{
    delete ui;
}

QListWidget *FramePanel::frameList() const
{
    return ui->FrameList;
}

QPushButton *FramePanel::addButton() const
{
    return ui->AddFrameButton;
}

QPushButton *FramePanel::addHoldButton() const
{
    return ui->AddHoldFrameButton;
}

QPushButton *FramePanel::deleteButton() const
{
    return ui->DeleteFrameButton;
}

QPushButton *FramePanel::playButton() const
{
    return ui->PlayButton;
}

QPushButton *FramePanel::pauseButton() const
{
    return ui->PauseButton;
}

QComboBox *FramePanel::fpsCombo() const
{
    return ui->FpsCombo;
}

int FramePanel::fpsForComboText(const QString &text, int fallback)
{
    const QString trimmed = text.trimmed();
    for (int i = 0; i < kCadenceCount; ++i) {
        if (trimmed == QString::fromUtf8(kCadences[i].title)) {
            return kCadences[i].fps;
        }
    }
    // A typed entry: a bare number, optionally with a unit. Deliberately a
    // WHOLE-string match rather than "the first number in the text" - the
    // cadence titles start with a digit ("1s with 4"), so a half-typed or
    // slightly-off cadence would otherwise resolve to 1 fps, which is a
    // plausible-looking wrong answer instead of a refusal.
    static const QRegularExpression numeric(
        QStringLiteral("^(\\d{1,3})\\s*(?:fps)?$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = numeric.match(trimmed);
    if (match.hasMatch()) {
        const int typed = match.captured(1).toInt();
        if (typed >= 1 && typed <= 120) {
            return typed;
        }
    }
    return fallback;
}

QString FramePanel::comboTextForFps(int fps)
{
    for (int i = 0; i < kCadenceCount; ++i) {
        if (kCadences[i].fps == fps) {
            return QString::fromUtf8(kCadences[i].title);
        }
    }
    Q_UNUSED(kBaseFps);
    return QString::number(fps);
}
