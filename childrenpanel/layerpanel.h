#ifndef LAYERPANEL_H
#define LAYERPANEL_H

#include <QWidget>

class QPushButton;
class QTreeWidget;

namespace Ui {
class LayerPanel;
}

class LayerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayerPanel(QWidget *parent = nullptr);
    ~LayerPanel();

    // A tree now: layer groups render as expandable parents, layers as leaves.
    QTreeWidget *layerList() const;
    QPushButton *addButton() const;
    QPushButton *deleteButton() const;
    QPushButton *unselectButton() const;

private:
    Ui::LayerPanel *ui;
};

#endif // LAYERPANEL_H
