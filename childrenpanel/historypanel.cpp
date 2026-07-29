#include "historypanel.h"

#include <QListWidget>
#include <QVBoxLayout>

HistoryPanel::HistoryPanel(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_historyList = new QListWidget(this);
    m_historyList->setObjectName(QStringLiteral("HistoryList"));
    m_historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_historyList);
}

QListWidget *HistoryPanel::historyList() const
{
    return m_historyList;
}
