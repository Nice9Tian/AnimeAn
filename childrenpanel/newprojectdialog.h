#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDialog>
#include <QSize>

class QSpinBox;
class QComboBox;

// Asks for the page size of a new document. Shown at startup and from
// File > New; cancelling leaves the caller's current size alone.
class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(const QSize &initial, QWidget *parent = nullptr);

    QSize canvasSize() const;

private:
    void applyPreset(int index);
    void syncPresetToSize();

    QSpinBox *m_widthBox = nullptr;
    QSpinBox *m_heightBox = nullptr;
    QComboBox *m_presetBox = nullptr;
    bool m_syncing = false;
};

#endif // NEWPROJECTDIALOG_H
