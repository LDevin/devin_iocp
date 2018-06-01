#include "mainwindow.h"
#include "ui_mainwindow.h"


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    std::cout<<m_client.displayMsg("Developer:%s, Email:%s", "devin", "295511204@qq.com") <<std::endl;
    m_client.getLocalIp();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_startBtn_clicked()
{
    if (m_client.loadSockLib()) {
        m_client.setIp("192.168.10.177");
        m_client.start();
    }

}

void MainWindow::on_stopBtn_clicked()
{

}

void MainWindow::on_quitBtn_clicked()
{

}
