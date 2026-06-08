#include "assetpanel.h"
#include "ui_assetpanel.h"

AssetPanel::AssetPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::AssetPanel)
{
    ui->setupUi(this);
}

AssetPanel::~AssetPanel()
{
    delete ui;
}

QListWidget *AssetPanel::assetList() const
{
    return ui->AssetList;
}

QPushButton *AssetPanel::addButton() const
{
    return ui->AddAssetButton;
}

QPushButton *AssetPanel::unselectButton() const
{
    return ui->UnselectAssetButton;
}
