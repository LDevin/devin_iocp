#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_server.getLocalIp();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_startBtn_clicked()
{
    if (m_server.startServer()) {
        qDebug() << "start server";
    } else {
        qDebug() << "start server failed";
    }
}

void MainWindow::on_stopBtn_clicked()
{
    m_server.stopServer();
}
