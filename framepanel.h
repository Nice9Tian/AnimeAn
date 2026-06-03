#ifndef FRAMEPANEL_H
#define FRAMEPANEL_H

#include <QWidget>

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
    QPushButton *deleteButton() const;

private:
    Ui::FramePanel *ui;
};

#endif // FRAMEPANEL_H
