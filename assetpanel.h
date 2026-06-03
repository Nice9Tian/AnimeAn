#ifndef ASSETPANEL_H
#define ASSETPANEL_H

#include <QWidget>

class QListWidget;
class QPushButton;

namespace Ui {
class AssetPanel;
}

class AssetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssetPanel(QWidget *parent = nullptr);
    ~AssetPanel();

    QListWidget *assetList() const;
    QPushButton *addButton() const;
    QPushButton *unselectButton() const;

private:
    Ui::AssetPanel *ui;
};

#endif // ASSETPANEL_H
