/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.9.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QOpenGLWidget *graphicsView;
    QPushButton *blueButton;
    QPushButton *greenButton;
    QPushButton *Pen;
    QPushButton *Eraser;
    QPushButton *LineErazer;
    QSlider *SmoothValue;
    QLabel *SmoothValue_print;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(800, 600);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        graphicsView = new QOpenGLWidget(centralwidget);
        graphicsView->setObjectName("graphicsView");
        graphicsView->setGeometry(QRect(20, 20, 551, 421));
        blueButton = new QPushButton(centralwidget);
        blueButton->setObjectName("blueButton");
        blueButton->setGeometry(QRect(580, 20, 121, 71));
        greenButton = new QPushButton(centralwidget);
        greenButton->setObjectName("greenButton");
        greenButton->setGeometry(QRect(580, 100, 121, 71));
        Pen = new QPushButton(centralwidget);
        Pen->setObjectName("Pen");
        Pen->setGeometry(QRect(580, 270, 121, 71));
        Eraser = new QPushButton(centralwidget);
        Eraser->setObjectName("Eraser");
        Eraser->setGeometry(QRect(580, 370, 121, 71));
        LineErazer = new QPushButton(centralwidget);
        LineErazer->setObjectName("LineErazer");
        LineErazer->setGeometry(QRect(580, 190, 121, 61));
        SmoothValue = new QSlider(centralwidget);
        SmoothValue->setObjectName("SmoothValue");
        SmoothValue->setGeometry(QRect(580, 470, 121, 22));
        SmoothValue->setMinimum(0);
        SmoothValue->setMaximum(100);
        SmoothValue->setValue(50);
        SmoothValue->setOrientation(Qt::Orientation::Horizontal);
        SmoothValue_print = new QLabel(centralwidget);
        SmoothValue_print->setObjectName("SmoothValue_print");
        SmoothValue_print->setGeometry(QRect(580, 500, 121, 31));
        SmoothValue_print->setAlignment(Qt::AlignmentFlag::AlignCenter);
        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 800, 22));
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
        blueButton->setText(QCoreApplication::translate("MainWindow", "Blue", nullptr));
        greenButton->setText(QCoreApplication::translate("MainWindow", "Green", nullptr));
        Pen->setText(QCoreApplication::translate("MainWindow", "Pen", nullptr));
        Eraser->setText(QCoreApplication::translate("MainWindow", "Eraser", nullptr));
        LineErazer->setText(QCoreApplication::translate("MainWindow", "LineErazer", nullptr));
        SmoothValue_print->setText(QCoreApplication::translate("MainWindow", "Smooth: 50", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
