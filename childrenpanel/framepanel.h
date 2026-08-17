#ifndef FRAMEPANEL_H
#define FRAMEPANEL_H

#include <QWidget>

class QComboBox;
class QListWidget;
class QPushButton;

namespace Ui {
class FramePanel;
}

class FramePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FramePanel(QWidget *parent = nullptr);
    ~FramePanel();

    QListWidget *frameList() const;
    QPushButton *addButton() const;
    QPushButton *addHoldButton() const;
    QPushButton *deleteButton() const;
    QPushButton *playButton() const;
    QPushButton *pauseButton() const;
    QComboBox *fpsCombo() const;

    // Shooting cadences against a 24 fps base, plus free numeric entry.
    static int fpsForComboText(const QString &text, int fallback);
    static QString comboTextForFps(int fps);

private:
    Ui::FramePanel *ui;
};

#endif // FRAMEPANEL_H
