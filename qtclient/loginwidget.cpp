#include "loginwidget.h"
#include "ui_loginwidget.h"  // 包含UI生成文件
#include "widget.h"
#include <QMessageBox>
#include <QJsonDocument>


//初始化界面
LoginWidget::LoginWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LoginWidget),
    socket(new QTcpSocket(this))
{
    ui->setupUi(this);
    setWindowTitle("云盘客户端 - 登录");
    ui->portEdit->setText("8000");
    ui->ipEdit->setText("192.168.112.10");

    // 连接网络信号
    connect(socket, &QTcpSocket::connected, this, &LoginWidget::on_connected);
    connect(socket, &QTcpSocket::readyRead, this, &LoginWidget::on_readyRead);
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(on_errorOccurred(QAbstractSocket::SocketError)));
}

LoginWidget::~LoginWidget()
{
    delete ui;
}
//发送json消息
void LoginWidget::sendJsonMessage(const QJsonObject &json)
{
    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    quint32 dataLen = static_cast<quint32>(jsonData.size());
    quint32 netLen = htonl(dataLen);

    QByteArray sendData;
    sendData.append(reinterpret_cast<const char*>(&netLen), 4);
    sendData.append(jsonData);
    socket->write(sendData);
}

//状态提示
void LoginWidget::showStatus(const QString &msg)
{
    ui->statusLabel->setText(msg);
}

//登录按钮被按下，发送登录请求
void LoginWidget::on_loginButton_clicked()
{
    QString username = ui->usernameEdit->text().trimmed();
    QString password = ui->passwordEdit->text().trimmed();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "用户名和密码不能为空");
        return;
    }

    if (socket->state() != QTcpSocket::ConnectedState) {
        QString ip = ui->ipEdit->text().trimmed();
        quint16 port = ui->portEdit->text().toUInt();
        socket->connectToHost(ip, port);
        showStatus("正在连接服务器...");
        // 记录当前操作类型（登录），用于连接成功后发送请求
        currentOperation = "login";
    } else {
        // 直接发送登录请求
        QJsonObject json;
        json["type"] = "login";  // 明确指定类型为登录
        json["username"] = username;
        json["password"] = password;
        sendJsonMessage(json);
        showStatus("发送登录请求...");  // 修正提示文本
    }
}


// 注册按钮点击事件（直接发送注册请求）
void LoginWidget::on_registerButton_clicked()
{
    QString username = ui->usernameEdit->text().trimmed();
    QString password = ui->passwordEdit->text().trimmed();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "用户名和密码不能为空");
        return;
    }

    if (socket->state() != QTcpSocket::ConnectedState) {
        QString ip = ui->ipEdit->text().trimmed();
        quint16 port = ui->portEdit->text().toUInt();
        socket->connectToHost(ip, port);
        showStatus("正在连接服务器...");
        // 记录当前操作类型（注册）
        currentOperation = "register";
    } else {
        // 直接发送注册请求
        QJsonObject json;
        json["type"] = "register";  // 明确指定类型为注册
        json["username"] = username;
        json["password"] = password;
        sendJsonMessage(json);
        showStatus("发送注册请求...");
    }
}


void LoginWidget::on_connected()
{
    showStatus("已连接到服务器");
    if (!ui->usernameEdit->text().isEmpty()) {
        // 根据记录的操作类型发送请求
        if (currentOperation == "login") {
            on_loginButton_clicked();  // 发送登录请求
        } else if (currentOperation == "register") {
            on_registerButton_clicked();  // 发送注册请求
        }
    }
}

//解析服务器
void LoginWidget::on_readyRead()
{
    // 1. 将新数据追加到缓存
    recvBuffer.append(socket->readAll());
    qDebug() << "当前缓存数据长度：" << recvBuffer.size();

    // 2. 循环解析缓存中的完整数据包（可能包含多个包）
    while (true) {
        // 2.1 先检查缓存中是否有4字节长度前缀
        if (recvBuffer.size() < 4) {
            qDebug() << "缓存数据不足4字节（长度前缀），等待更多数据";
            break;  // 跳出循环，等待下次readyRead
        }

        // 2.2 解析长度前缀（从缓存中提取）
        quint32 netLen;
        memcpy(&netLen, recvBuffer.data(), 4);  // 从缓存取前4字节
        quint32 dataLen = ntohl(netLen);
        qDebug() << "解析出数据长度：" << dataLen;

        // 2.3 检查缓存中是否有完整的JSON数据
        if (recvBuffer.size() < 4 + dataLen) {
            qDebug() << "JSON数据不完整（需要" << 4 + dataLen << "，当前" << recvBuffer.size() << "）";
            break;  // 跳出循环，等待下次readyRead
        }

        // 2.4 提取完整的JSON数据（跳过前4字节长度）
        QByteArray jsonData = recvBuffer.mid(4, dataLen);
        // 移除已处理的数据（前4字节+dataLen字节）
        recvBuffer.remove(0, 4 + dataLen);

        // 2.5 解析JSON
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (!doc.isObject()) {
            qDebug() << "JSON解析失败，不是有效的对象";
            showStatus("收到无效数据");
            continue;  // 继续解析下一个包
        }

        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        qDebug() << "解析出type字段：" << type;
        qDebug() << "解析出success字段：" << json["success"].toBool();

        // 处理登录结果
        if (type == "login_result") {
            bool success = json["success"].toBool();
            if (success) {
                qDebug() << "客户端确认登录成功，准备跳转界面";
                showStatus("登录成功，进入云盘...");
                Widget *clientWidget = new Widget(socket, ui->usernameEdit->text().trimmed());
                clientWidget->show();


                socket->setParent(clientWidget);
                // 精准断开 LoginWidget 自己的槽函数，保留 socket 的信号能力
                // 1. 断开 LoginWidget 的 readyRead 槽
                disconnect(socket, &QTcpSocket::readyRead, this, &LoginWidget::on_readyRead);
                // 2. 断开 LoginWidget 的 error 槽（解决弹窗两次的问题）
                disconnect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
                           this, SLOT(on_errorOccurred(QAbstractSocket::SocketError)));
                clientWidget->setRecvBuffer(this->recvBuffer);
                this->recvBuffer.clear();

                clientWidget->show();




                QMetaObject::invokeMethod(clientWidget,"on_readyRead",Qt::QueuedConnection);

                this->close();
            } else {
                qDebug() << "登录失败，原因：" << json["message"].toString();
                QMessageBox::warning(this, "登录失败", json["message"].toString());
            }
        }else if (type == "register_result") {
            bool success = json["success"].toBool();
            QString msg = json["message"].toString();
            if (success) {
                QMessageBox::information(this, "注册成功", msg);
            } else {
                QMessageBox::warning(this, "注册失败", msg);
            }
        }else {
            // 只忽略非登录/注册响应，不打印"未知类型"（避免干扰）
            qDebug() << "LoginWidget忽略非登录响应：" << type;
        }
    }
}

void LoginWidget::on_errorOccurred(QAbstractSocket::SocketError error)
{
    QMessageBox::critical(this, "网络错误", socket->errorString());
    showStatus("连接失败：" + socket->errorString());
}
