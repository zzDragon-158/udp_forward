#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "client.h"
#include <streambuf>
#include <QTextBrowser>
// #include <QScrollBar>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onPushButtonClientRunClicked();
    void onPushButtonServerRunClicked();

private:
    Ui::MainWindow *ui;
    void saveClientConfigToJson();
    void saveServerConfigToJson();
    void loadConfigFromJson();
    void setClientConfigReadOnly(bool flag);
    void setServerConfigReadOnly(bool flag);
    void periodTask();
};

// 自定义流缓冲区
class TextBrowserStreamBuf : public std::streambuf {
public:
    TextBrowserStreamBuf(QTextBrowser* textBrowser) : textBrowser(textBrowser) {}

protected:
    // 缓存输出的字符串
    std::string buffer;
    std::mutex bufferMutex;

    int overflow(int c) override {
        std::lock_guard<mutex> lock(bufferMutex);
        if (c != EOF) {
            buffer += static_cast<char>(c);
        }
        return c;
    }

    int sync() override {
        std::lock_guard<mutex> lock(bufferMutex);
        if (!buffer.empty()) {
            /* protect textBrowser */ {
                textBrowser->append(QString::fromStdString(buffer));
                textBrowser->update();
                buffer.clear();
            }
        }
        return 0; // 成功
    }

private:
    QTextBrowser* textBrowser;
};

#endif // MAINWINDOW_H
