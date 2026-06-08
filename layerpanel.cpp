#include "layerpanel.h"
#include "ui_layerpanel.h"

LayerPanel::LayerPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LayerPanel)
{
    ui->setupUi(this);
}

LayerPanel::~LayerPanel()
{
    delete ui;
}

QListWidget *LayerPanel::layerList() const
{
    return ui->LayerList;
}

QPushButton *LayerPanel::addButton() const
{
    return ui->AddLayerButton;
}

QPushButton *LayerPanel::deleteButton() const
{
    return ui->DeleteLayerButton;
}

QPushButton *LayerPanel::unselectButton() const
{
    return ui->UnselectLayerButton;
}
