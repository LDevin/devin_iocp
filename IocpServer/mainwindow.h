#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "iocpserver.h"


namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_startBtn_clicked();

    void on_stopBtn_clicked();

private:
    Ui::MainWindow *ui;

    IOCPServer m_server;
};

#endif // MAINWINDOW_H
