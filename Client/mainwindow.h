#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStringListModel>
#include "clientsocket.h"


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

    void on_quitBtn_clicked();

private:
    Ui::MainWindow *ui;

    Client m_client;
    QStringListModel msgLisModel;
};

#endif // MAINWINDOW_H
