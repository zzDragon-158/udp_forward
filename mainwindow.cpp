#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "client.h"
#include "server.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

#define storeCfgFilePath    "data.json"

// 声明全局 QTextBrowser
TextBrowserStreamBuf* globalBuffer = nullptr;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    loadConfigFromJson();

    ui->textBrowser_log_display->setStyleSheet  ("QTextBrowser {"
                                                    "background-color: lightgreen;"
                                                    // "background-image: url('path/to/your/image.jpg');"
                                                    // "background-repeat: no-repeat;"
                                                    // "background-position: center;"
                                                    // "background-attachment: scroll;"
                                                "}");
    ui->lcdNumber->setSegmentStyle(QLCDNumber::Flat);
    ui->lcdNumber->setStyleSheet("QLCDNumber { background-color: white; color: black; border: 1px solid black; }");
    ui->group_client->setStyleSheet("QGroupBox { background-color: lightgreen; border: 1px solid black;}");
    ui->group_server->setStyleSheet("QGroupBox { background-color: lightgreen; border: 1px solid black;}");
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
        setClientConfigReadOnly(true);
        saveClientConfigToJson();
    } else {
        pClient->stop();
        if (pClientThread->joinable())
            pClientThread->join();
        delete pClientThread;
        pClientThread = nullptr;
        delete pClient;
        pClient = nullptr;
        ui->pushButton_runClient->setText("运行客户端");
        setClientConfigReadOnly(false);
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
        setServerConfigReadOnly(true);
        saveServerConfigToJson();
    } else {
        pServer->stop();
        if (pServerThread->joinable())
            pServerThread->join();
        delete pServerThread;
        pServerThread = nullptr;
        delete pServer;
        pServer = nullptr;
        ui->pushButton_runServer->setText("运行服务端");
        setServerConfigReadOnly(false);
    }
    ui->pushButton_runServer->setEnabled(true);
}

void MainWindow::setLcdNumber(int number) {
    ui->lcdNumber->display(number);
}

void MainWindow::saveClientConfigToJson() {
    QFile file(storeCfgFilePath);
    QJsonObject json;

    if (file.open(QFile::ReadOnly)) {
        QByteArray data = file.readAll();
        QJsonDocument doc(QJsonDocument::fromJson(data));
        json = doc.object();
        file.close();
    }
    json["clientServerIPv6Address"] = ui->lineEdit_clientServerIPv6Address->text();
    json["clientServerPort"] = ui->lineEdit_clientServerPort->text();
    json["clientRemotePort"] = ui->lineEdit_clientRemotePort->text();
    json["clientLocalPort"] = ui->lineEdit_clientLocalPort->text();
    if (file.open(QFile::WriteOnly)) {
        QJsonDocument doc(json);
        file.write(doc.toJson());
        file.close();
        LogDebug("Success", "Data saved successfully!");
    } else {
        LogError("Error", "Could not save data.")
    }
}

void MainWindow::saveServerConfigToJson() {
    QFile file(storeCfgFilePath);
    QJsonObject json;

    if (file.open(QFile::ReadOnly)) {
        QByteArray data = file.readAll();
        QJsonDocument doc(QJsonDocument::fromJson(data));
        json = doc.object();
        file.close();
    }
    json["serverRemotePort"] = ui->lineEdit_serverRemotePort->text();
    json["serverServerPort"] = ui->lineEdit_serverServerPort->text();
    if (file.open(QFile::WriteOnly)) {
        QJsonDocument doc(json);
        file.write(doc.toJson());
        file.close();
        LogDebug("Success", "Data saved successfully!");
    } else {
        LogError("Error", "Could not save data.")
    }
}

void MainWindow::loadConfigFromJson() {
    QFile file(storeCfgFilePath);
    if (file.open(QFile::ReadOnly)) {
        QByteArray data = file.readAll();
        QJsonDocument doc(QJsonDocument::fromJson(data));
        QJsonObject json = doc.object();

        // 设置控件的值
        if (json.contains("serverRemotePort")) {
            ui->lineEdit_serverRemotePort->setText(json["serverRemotePort"].toString());
        }
        if (json.contains("serverServerPort")) {
            ui->lineEdit_serverServerPort->setText(json["serverRemotePort"].toString());
        }
        if (json.contains("clientServerIPv6Address")) {
            ui->lineEdit_clientServerIPv6Address->setText(json["clientServerIPv6Address"].toString());
        }
        if (json.contains("clientServerPort")) {
            ui->lineEdit_clientServerPort->setText(json["clientServerPort"].toString());
        }
        if (json.contains("clientRemotePort")) {
            ui->lineEdit_clientRemotePort->setText(json["clientRemotePort"].toString());
        }
        if (json.contains("clientLocalPort")) {
            ui->lineEdit_clientLocalPort->setText(json["clientLocalPort"].toString());
        }
        file.close();
    }
}

void MainWindow::setClientConfigReadOnly(bool flag) {
    ui->lineEdit_clientServerIPv6Address->setReadOnly(flag);
    ui->lineEdit_clientServerPort->setReadOnly(flag);
    ui->lineEdit_clientRemotePort->setReadOnly(flag);
    ui->lineEdit_clientLocalPort->setReadOnly(flag);
}

void MainWindow::setServerConfigReadOnly(bool flag) {
    ui->lineEdit_serverRemotePort->setReadOnly(flag);
    ui->lineEdit_serverServerPort->setReadOnly(flag);
}
