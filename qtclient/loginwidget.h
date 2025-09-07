#ifndef LOGINWIDGET_H
#define LOGINWIDGET_H

#include <QWidget>
#include <QTcpSocket>
#include <QJsonObject>
#include <winsock2.h>  // Windows 系统


namespace Ui {
class LoginWidget;  // 与UI类名一致
}

class LoginWidget : public QWidget
{
    Q_OBJECT

public:

    explicit LoginWidget(QWidget *parent = nullptr);
    ~LoginWidget();

signals:
    void loginSuccess(QTcpSocket *socket, const QString &username);

private slots:
    void on_loginButton_clicked();
    void on_registerButton_clicked();
    void on_connected();
    void on_readyRead();
    void on_errorOccurred(QAbstractSocket::SocketError socketError);

private:
    Ui::LoginWidget *ui;  // UI指针
    QTcpSocket *socket;   //
    QString currentOperation;  // 用于记录"login"或"register"
    QByteArray recvBuffer;  // 缓存接收的数据
    void sendJsonMessage(const QJsonObject &json);
    void showStatus(const QString &msg);
};

#endif // LOGINWIDGET_H
