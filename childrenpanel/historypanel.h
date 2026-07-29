#ifndef HISTORYPANEL_H
#define HISTORYPANEL_H

#include <QWidget>

class QListWidget;

class HistoryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPanel(QWidget *parent = nullptr);

    QListWidget *historyList() const;

private:
    QListWidget *m_historyList = nullptr;
};

#endif // HISTORYPANEL_H
