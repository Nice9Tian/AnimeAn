#ifndef LAYERPANEL_H
#define LAYERPANEL_H

#include <QWidget>

class QListWidget;
class QPushButton;

namespace Ui {
class LayerPanel;
}

class LayerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayerPanel(QWidget *parent = nullptr);
    ~LayerPanel();

    QListWidget *layerList() const;
    QPushButton *addButton() const;
    QPushButton *deleteButton() const;
    QPushButton *unselectButton() const;

private:
    Ui::LayerPanel *ui;
};

#endif // LAYERPANEL_H
