#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "client.h"
#include "server.h"
#include <QHostAddress>

// 声明全局 QTextBrowser
TextBrowserStreamBuf* globalBuffer = nullptr;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 重定向 stdout
    globalBuffer = new TextBrowserStreamBuf(ui->textBrowser_log_display);
    std::cerr.rdbuf(globalBuffer);
    std::cout.rdbuf(globalBuffer);

    connect(ui->pushButton_runClient, &QPushButton::clicked, this, &MainWindow::onPushButtonClientRunClicked);
    connect(ui->pushButton_runServer, &QPushButton::clicked, this, &MainWindow::onPushButtonServerRunClicked);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onPushButtonClientRunClicked() {
    ui->pushButton_runClient->setEnabled(false);
    // 创建/销毁Client实例
    static std::thread* pClientThread = nullptr;
    static Client* pClient = nullptr;
    if (pClient == nullptr) {
        // 校验 IPv6 地址
        QHostAddress address;
        if (!address.setAddress(ui->lineEdit_clientServerIPv6Address->text())) {
            LogError("服务器IPV6地址无效");
            ui->pushButton_runClient->setEnabled(true);
            return;
        }
        // 校验端口号
        bool validPort;
        uint16_t serverPort = ui->lineEdit_clientServerPort->text().toUShort(&validPort);
        if (!validPort || serverPort == 0 || serverPort > 65535) {
            LogError("服务器端口号无效");
            ui->pushButton_runClient->setEnabled(true);
            return;
        }

        uint16_t remotePort = ui->lineEdit_clientRemotePort->text().toUShort(&validPort);
        if (!validPort || remotePort == 0 || remotePort > 65535) {
            LogError("远程端口号无效");
            ui->pushButton_runClient->setEnabled(true);
            return;
        }

        uint16_t localPort = ui->lineEdit_clientLocalPort->text().toUShort(&validPort);
        if (!validPort || localPort == 0 || localPort > 65535) {
            LogError("本地端口号无效");
            ui->pushButton_runClient->setEnabled(true);
            return;
        }
        pClient = new Client(ui->lineEdit_clientServerIPv6Address->text().toUtf8().constData(), serverPort, remotePort, localPort);
        pClientThread = new std::thread(Client::start, std::ref(*pClient));
        ui->pushButton_runClient->setText("停止客户端");
    } else {
        pClient->stop();
        if (pClientThread->joinable())
            pClientThread->join();
        delete pClientThread;
        pClientThread = nullptr;
        delete pClient;
        pClient = nullptr;
        ui->pushButton_runClient->setText("运行客户端");
    }
    ui->pushButton_runClient->setEnabled(true);
}

void MainWindow::onPushButtonServerRunClicked() {
    ui->pushButton_runServer->setEnabled(false);
    // 创建/销毁Client实例
    static std::thread* pServerThread = nullptr;
    static Server* pServer = nullptr;
    if (pServer == nullptr) {
        // 校验端口号
        bool validPort;
        uint16_t serverPort = ui->lineEdit_serverServerPort->text().toUShort(&validPort);
        if (!validPort || serverPort == 0 || serverPort > 65535) {
            LogError("服务器端口号无效");
            ui->pushButton_runServer->setEnabled(true);
            return;
        }

        uint16_t remotePort = ui->lineEdit_serverRemotePort->text().toUShort(&validPort);
        if (!validPort || remotePort == 0 || remotePort > 65535) {
            LogError("远程端口号无效");
            ui->pushButton_runServer->setEnabled(true);
            return;
        }
        pServer = new Server(serverPort, remotePort);
        pServerThread = new std::thread(Server::start, std::ref(*pServer));
        ui->pushButton_runServer->setText("停止服务端");
    } else {
        pServer->stop();
        if (pServerThread->joinable())
            pServerThread->join();
        delete pServerThread;
        pServerThread = nullptr;
        delete pServer;
        pServer = nullptr;
        ui->pushButton_runServer->setText("运行服务端");
    }
    ui->pushButton_runServer->setEnabled(true);
}
