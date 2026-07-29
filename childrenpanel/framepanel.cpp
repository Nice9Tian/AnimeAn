#include "framepanel.h"
#include "ui_framepanel.h"

FramePanel::FramePanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FramePanel)
{
    ui->setupUi(this);
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
