#include "widget.h"
#include "ui_widget.h"
#include "loginwidget.h"
#include "historydialog.h"  // 确保包含历史对话框头文件
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QtEndian>

// 常量定义（建议放在头文件，此处临时定义确保编译）
const int Widget::BUFFER_SIZE = 4096;  // 4KB 缓冲区，可根据需求调整

// ========================== 构造/析构函数 ==========================
Widget::Widget(QTcpSocket *socket, const QString &username, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Widget),
    socket(socket),
    currentPath("/"),
    m_username(username),
    transferState(TransferState::Idle),  // 初始化传输状态为空闲
    uploadedSize(0),
    totalUploadSize(0),
    downloadFile(nullptr),
    downloadedSize(0),
    totalDownloadSize(0),
    isReadyToSendReceived(false)
{
    ui->setupUi(this);
    setWindowTitle("云盘客户端 - " + m_username);

    // 信号连接：网络状态与消息接收
    connect(socket, &QTcpSocket::disconnected, this, &Widget::on_disconnected);
    connect(socket, &QTcpSocket::readyRead, this, &Widget::on_readyRead);
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(on_errorOccurred(QAbstractSocket::SocketError)));
    // 新增：监听数据实际发送（驱动异步上传）
    connect(socket, &QTcpSocket::bytesWritten, this, &Widget::onBytesWritten, Qt::UniqueConnection);

    // 界面交互信号
    connect(ui->fileListWidget, &QListWidget::itemDoubleClicked,
            this, &Widget::onFileListDoubleClicked,
            Qt::UniqueConnection);

    // 初始化界面
    requestFileList();
    updatePathDisplay();
    showStatus("请求文件列表...");
}

Widget::~Widget()
{
    // 清理历史对话框
    if (m_historyDialog) {
        m_historyDialog->close();
    }

    // 释放文件资源
    cleanupUpload();
    cleanupDownload();
    delete ui;
}

// ========================== 基础工具函数 ==========================
void Widget::sendJsonMessage(const QJsonObject &json)
{
    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    quint32 dataLen = static_cast<quint32>(jsonData.size());
    quint32 netLen = qToBigEndian(dataLen);  // 转为网络字节序（大端）

    QByteArray sendData;
    sendData.append(reinterpret_cast<const char*>(&netLen), 4);  // 4字节长度前缀
    sendData.append(jsonData);
    socket->write(sendData);
}

void Widget::showStatus(const QString &msg)
{
    ui->statusLabel->setText(msg);
}

void Widget::requestFileList()
{
    QJsonObject json;
    json["type"] = "list";
    json["path"] = currentPath;
    sendJsonMessage(json);
}

void Widget::updatePathDisplay()
{
    ui->pathLabel->setText(currentPath);
}

// ========================== 上传相关函数 ==========================
void Widget::on_uploadButton_clicked()
{
    qDebug() << "上传按钮触发";
    if (transferState != TransferState::Idle) {
        QMessageBox::information(this, "提示", "当前有传输任务正在进行");
        return;
    }

    // 选择上传文件
    QString filePath = QFileDialog::getOpenFileName(this, "选择上传文件", QDir::homePath());
    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    uploadFile = new QFile(filePath, this);
    if (!uploadFile->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "无法打开文件：" + uploadFile->errorString());
        delete uploadFile;
        uploadFile = nullptr;
        return;
    }

    // 记录文件总大小（用于后续校验）
    totalUploadSize = fileInfo.size();
    qDebug() << "[上传] 文件总大小：" << totalUploadSize << "字节";

    // 发送上传请求（告知服务器文件名、大小、路径）
    QJsonObject json;
    json["type"] = "upload";
    json["filename"] = fileInfo.fileName();
    json["size"] = totalUploadSize;
    json["path"] = currentPath;
    sendJsonMessage(json);

    transferState = TransferState::Uploading;
    showStatus("等待服务器准备接收...");
}

void Widget::cleanupUpload()
{
    // 释放上传文件资源
    if (uploadFile) {
        uploadFile->close();
        delete uploadFile;
        uploadFile = nullptr;
    }
    // 重置上传状态（仅在收到服务器确认后调用，避免异步发送冲突）
    uploadedSize = 0;
    totalUploadSize = 0;
    uploadBuffer.clear();  // 清空未发送缓存
    ui->progressBar->setValue(0);
    showStatus("上传已停止或失败");
}

// 【核心】处理服务器"准备接收"消息（触发第一次数据发送）
void Widget::handleReadyToReceiveMsg()
{
    showStatus("开始上传文件数据...");
    if (!uploadFile) {
        cleanupUpload();
        transferState = TransferState::Idle;
        return;
    }

    // 校验文件读取完整性（避免文件被占用导致读取不完整）
    qint64 remainingReadSize = uploadFile->size() - uploadFile->pos();
    if (uploadedSize + remainingReadSize < totalUploadSize) {
        QMessageBox::warning(this, "读取错误", "文件未完全读取（可能被其他程序占用）");
        cleanupUpload();
        transferState = TransferState::Idle;
        return;
    }

    // 触发第一次数据发送（后续由 bytesWritten 信号驱动）
    sendNextUploadData();
}

// 发送下一批上传数据（由 bytesWritten 信号驱动，处理异步发送）
void Widget::sendNextUploadData()
{
    // 1. 先判断是否已发送完成（避免重复处理）
    if (uploadedSize >= totalUploadSize && uploadBuffer.isEmpty()) {
        uploadFile->close();
        delete uploadFile;
        uploadFile = nullptr;
        showStatus("文件上传完成，等待服务器确认...");
        return;
    }

    // 2. 缓存为空时，从文件读取新数据（填充缓冲区）
    if (uploadBuffer.isEmpty() && uploadedSize < totalUploadSize) {
        uploadBuffer = uploadFile->read(BUFFER_SIZE);
        qDebug() << "[上传] 从文件读取：" << uploadBuffer.size() << "字节";

        // 特殊情况：文件已读完，但缓存为空 → 说明所有数据已发送完成
        if (uploadBuffer.isEmpty() && uploadedSize >= totalUploadSize) {
            uploadFile->close();
            delete uploadFile;
            uploadFile = nullptr;
            showStatus("文件上传完成，等待服务器确认...");
            return;
        }
    }

    // 3. 发送缓存中的数据（异步写入操作系统缓冲区）
    if (!uploadBuffer.isEmpty()) {
        qint64 sent = socket->write(uploadBuffer);
        if (sent < 0) {
            QMessageBox::warning(this, "上传错误", "网络发送失败：" + socket->errorString());
            cleanupUpload();
            transferState = TransferState::Idle;
            return;
        }

        // 4. 更新上传进度（仅记录写入缓冲区的字节数）
        uploadedSize += sent;
        uploadBuffer = uploadBuffer.mid(sent);  // 保留未发送的剩余数据
        qDebug() << "[上传] 写入缓冲区：" << sent << "字节，累计：" << uploadedSize << "字节，剩余缓存：" << uploadBuffer.size() << "字节";

        // 更新进度条
        int percent = (totalUploadSize > 0) ? static_cast<int>((uploadedSize * 100.0) / totalUploadSize) : 0;
        ui->progressBar->setValue(percent);
        showStatus(QString("上传中：%1/%2 字节（%3%）")
                   .arg(uploadedSize).arg(totalUploadSize).arg(percent));
    }
    // 5. 缓存为空且未发送完 → 真正的发送不完整（网络异常）
    else if (uploadedSize < totalUploadSize) {
        QMessageBox::warning(this, "上传失败", "文件发送不完整（网络中断或服务器异常）");
        cleanupUpload();
        transferState = TransferState::Idle;
    }
}

// 【信号回调】数据实际发送到网络后，继续发送剩余数据
void Widget::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);  // bytes 为实际发送到网络的字节数，此处无需额外处理

    // 仅在上传状态时，继续发送剩余数据
    if (transferState == TransferState::Uploading) {
        sendNextUploadData();
    }
}

// 【辅助】处理服务器上传结果确认
void Widget::handleUploadResultMsg(const QJsonObject &json)
{
    bool success = json["success"].toBool();
    QString msg = json["message"].toString();
    if (success) {
        QMessageBox::information(this, "上传成功", msg);
        requestFileList();  // 刷新文件列表
    } else {
        QMessageBox::warning(this, "上传失败", msg);
    }

    // 收到服务器确认后，重置为空闲状态
    transferState = TransferState::Idle;
    cleanupUpload();  // 清理上传资源
    ui->progressBar->setValue(0);
}

// 【辅助】处理上传续传消息（断点续传场景）
void Widget::handleUploadResumeMsg(const QJsonObject &json)
{
    qint64 offset = json["offset"].toVariant().toLongLong();
    if (uploadFile && uploadFile->isOpen()) {
        if (uploadFile->seek(offset)) {
            uploadedSize = offset;  // 从续传位置开始
            showStatus(QString("继续上传：从 %1 字节开始").arg(offset));
            sendNextUploadData();  // 触发续传
        } else {
            QMessageBox::warning(this, "续传错误", "无法定位到续传位置");
            cleanupUpload();
            transferState = TransferState::Idle;
        }
    }
    transferState = TransferState::Uploading;
}

// ========================== 下载相关函数 ==========================
void Widget::on_downloadButton_clicked()
{
    if (transferState != TransferState::Idle) {
        QMessageBox::information(this, "提示", "当前有传输任务正在进行");
        return;
    }

    // 检查是否选择文件
    QListWidgetItem *selectedItem = ui->fileListWidget->currentItem();
    if (!selectedItem) {
        QMessageBox::warning(this, "提示", "请先选择要下载的文件");
        return;
    }

    // 检查是否为目录（不允许下载目录）
    QString fileName = selectedItem->text();
    bool isDir = false;
    for (const auto& info : fileList) {
        if (info.name == fileName) {
            isDir = info.isDirectory;
            break;
        }
    }
    if (isDir) {
        QMessageBox::warning(this, "提示", "不能下载目录，请选择文件");
        return;
    }

    // 发送下载请求
    QJsonObject json;
    json["type"] = "download";
    json["filename"] = fileName;
    json["path"] = currentPath;
    sendJsonMessage(json);
    transferState = TransferState::WaitingDownloadMeta;
    showStatus("发送下载请求：" + fileName);
}

void Widget::cleanupDownload()
{
    // 释放下载文件资源
    if (downloadFile) {
        downloadFile->close();
        delete downloadFile;
        downloadFile = nullptr;
    }
    // 重置下载状态
    transferState = TransferState::Idle;
    isReadyToSendReceived = false;
    downloadedSize = 0;
    totalDownloadSize = 0;
    downloadFileName.clear();
    recvBuffer.clear();  // 清空接收缓存
    ui->progressBar->setValue(0);
}

// 【核心】处理下载二进制数据
void Widget::handleDownloadData()
{
    //showStatus("已进入handleDownloadData函数");
    if (!downloadFile || !downloadFile->isOpen()) return;

    //showStatus("已进入handleDownloadData函数a");
    // 读取所有可用数据
    QByteArray data = recvBuffer;
    if (data.isEmpty()) {qDebug()<<"data.isEmpty()"<<endl;return;}
    //showStatus("已进入handleDownloadData函数1");
    recvBuffer.clear();

    // 写入文件并更新进度
    // 计算实际可写入的字节数（不超过剩余需要的大小）
    qint64 remaining = totalDownloadSize - downloadedSize;
    qint64 writeLen = downloadFile->write(data.left(remaining));
    if (writeLen < 0) {
        QMessageBox::warning(this, "下载错误", "写入文件失败：" + downloadFile->errorString());
        cleanupDownload();
        return;
    }
    //showStatus("已进入handleDownloadData函数2");
    downloadedSize += writeLen;
    qDebug()<<"downloadedSize"<<downloadedSize<<endl;
    qDebug()<<"totalDownloadSize"<<totalDownloadSize<<endl;
    int percent = (totalDownloadSize > 0) ? static_cast<int>((downloadedSize * 100.0) / totalDownloadSize) : 0;
    ui->progressBar->setValue(percent);
    showStatus(QString("下载中：%1/%2 字节（%3%）")
               .arg(downloadedSize).arg(totalDownloadSize).arg(percent));

    // 下载完成判断
    if (downloadedSize >= totalDownloadSize) {
        int percent = 100;
        ui->progressBar->setValue(percent);
        QString savedFileName = downloadFile->fileName();
        downloadFile->close();
        delete downloadFile;
        downloadFile = nullptr;
        transferState = TransferState::Idle;
        showStatus("文件下载完成：" + downloadFileName);
        QMessageBox::information(this, "下载成功", "文件已保存至：" + QDir::toNativeSeparators(savedFileName));
    }
    //showStatus("已进入handleDownloadData函数3");
}

// 【辅助】处理服务器下载元信息（文件名、大小）
void Widget::handleDownloadMetaMsg(const QJsonObject &json)
{
    downloadFileName = json["filename"].toString();
    totalDownloadSize = json["size"].toVariant().toLongLong();

    // 选择保存路径
    QString savePath = QFileDialog::getSaveFileName(
                this, "保存文件", QDir::homePath() + "/" + downloadFileName,
                QString("%1 文件 (*.%2);;所有文件 (*.*)").arg(downloadFileName.section('.', -1).toUpper()).arg(downloadFileName.section('.', -1))
                );
    if (savePath.isEmpty()) {
        // 用户取消保存，重置状态
        QJsonObject cancelJson;
        cancelJson["type"] = "download_cancel";
        sendJsonMessage(cancelJson);
        transferState = TransferState::Idle;
        return;
    }

    // 创建并打开下载文件
    downloadFile = new QFile(savePath, this);
    if (!downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "错误", "无法创建文件：" + downloadFile->errorString());
        delete downloadFile;
        downloadFile = nullptr;
        transferState = TransferState::Idle;
        return;
    }

    // 初始化下载状态
    downloadedSize = 0;
    isReadyToSendReceived = false;
    transferState = TransferState::Downloading;
    showStatus("等待服务器发送文件数据...");

    // 发送"准备接收"确认
    QJsonObject ackJson;
    ackJson["type"] = "ready_to_receive";
    sendJsonMessage(ackJson);
}

// 【辅助】处理下载控制消息（如"准备发送数据"）
void Widget::handleDownloadControlLogic()
{
    while (true) {
        // 1. 先检查缓冲区是否够4字节（控制消息必须有4字节长度前缀）
        if (recvBuffer.size() < 4) {
            qDebug() << "控制消息：缓冲区不足4字节，退出";
            break;
        }

        // 2. 解析长度（大端序，与服务器一致）
        quint32 netLen;
        memcpy(&netLen, recvBuffer.data(), 4);
        quint32 dataLen = qFromBigEndian(netLen);
        qDebug() << "控制消息：解析长度=" << dataLen;

        // 关键1：如果长度为0，说明是文件数据（不是控制消息），直接退出
        if (dataLen == 0) {
            qDebug() << "控制消息：解析长度为0（是文件数据），退出处理";
            break;
        }

        // 3. 检查控制消息是否完整（4字节前缀 + dataLen字节JSON）
        if (recvBuffer.size() < 4 + dataLen) {
            qDebug() << "控制消息：数据不完整，退出";
            break;
        }

        // 4. 提取JSON并解析（仅处理有效控制消息）
        QByteArray jsonData = recvBuffer.mid(4, dataLen);
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

        // 关键2：解析失败 → 不是有效控制消息（是文件数据），不删数据，直接退出
        if (parseError.error != QJsonParseError::NoError) {
            qDebug() << "控制消息：解析失败（非有效JSON），退出：" << parseError.errorString();
            break;
        }

        // 5. 只有解析成功，才删除缓冲区中的控制消息（避免误删文件数据）
        recvBuffer.remove(0, 4 + dataLen);

        // 6. 只处理"ready_to_send"（其他控制消息已注释，服务器不应再发）
        QJsonObject json = doc.object();
        QString type = json["type"].toString();
        if (type == "ready_to_send") {
            isReadyToSendReceived = true;
            showStatus("开始接收文件数据...");
            qDebug() << "控制消息：收到ready_to_send，后续数据均视为文件数据";
            continue;
        } else {
            qDebug() << "控制消息：未知类型，跳过：" << type;
            continue;
        }
    }
}

// ========================== 文件管理相关函数 ==========================
void Widget::on_deleteButton_clicked()
{
    QListWidgetItem *selectedItem = ui->fileListWidget->currentItem();
    if (!selectedItem) {
        QMessageBox::warning(this, "提示", "请先选择要删除的文件或目录");
        return;
    }

    QString fileName = selectedItem->text();
    // 确认删除（处理目录后缀"/"）
    QString actualName = fileName.endsWith("/") ? fileName.left(fileName.size() - 1) : fileName;

    if (QMessageBox::question(this, "确认删除",
                              QString("确定要删除 %1 吗？删除后无法恢复！").arg(actualName)) != QMessageBox::Yes) {
        return;
    }

    // 发送删除请求
    QJsonObject json;
    json["type"] = "delete";
    json["filename"] = actualName;
    json["path"] = currentPath;
    sendJsonMessage(json);
    showStatus("发送删除请求：" + actualName);
}

void Widget::on_refreshButton_clicked()
{
    requestFileList();
    showStatus("刷新文件列表...");
}

void Widget::on_backButton_clicked()
{
    if (currentPath == "/") {
        QMessageBox::information(this, "提示", "已经是根目录");
        return;
    }

    // 计算上级目录路径（处理多级目录）
    int lastSlash = currentPath.lastIndexOf('/', currentPath.size() - 2);  // 跳过末尾的"/"
    currentPath = (lastSlash == -1) ? "/" : currentPath.left(lastSlash + 1);
    currentPath.replace("//", "/");  // 清理重复的"/"

    updatePathDisplay();
    requestFileList();
    showStatus("返回上级目录：" + currentPath);
}

void Widget::onFileListDoubleClicked(QListWidgetItem *item)
{
    if (!item) return;

    QString displayName = item->text();
    QString actualName = displayName.endsWith("/") ? displayName.left(displayName.size() - 1) : displayName;

    // 检查是否为目录
    for (const auto& info : fileList) {
        if (info.name == actualName && info.isDirectory) {
            // 计算新目录路径
            QString newPath = currentPath == "/" ? "/" + actualName : currentPath + "/" + actualName;
            newPath.replace("//", "/");  // 清理重复"/"
            if (!newPath.endsWith("/")) newPath += "/";  // 确保目录路径以"/"结尾

            currentPath = newPath;
            updatePathDisplay();
            requestFileList();
            showStatus("进入目录：" + actualName);
            return;
        }
    }

    // 若为文件，仅提示选择
    showStatus("选择了文件：" + actualName);
}

// 【辅助】处理服务器文件列表响应
void Widget::handleFileListMsg(const QJsonObject &json)
{
    ui->fileListWidget->clear();
    fileList.clear();

    QJsonArray files = json["files"].toArray();
    for (const QJsonValue &val : files) {
        QJsonObject fileObj = val.toObject();
        QString name = fileObj["name"].toString();
        bool isDir = fileObj["is_directory"].toBool();

        // 存储文件信息
        fileList.push_back(FileInfo(name, isDir));
        // 创建列表项（目录后缀加"/"区分）
        QListWidgetItem *item = new QListWidgetItem(isDir ? name + "/" : name);
        ui->fileListWidget->addItem(item);
    }

    showStatus(QString("文件列表更新，共 %1 个项目").arg(files.size()));
}

// 【辅助】处理服务器删除结果响应
void Widget::handleDeleteResultMsg(const QJsonObject &json)
{
    bool success = json["success"].toBool();
    QString msg = json["message"].toString();
    if (success) {
        QMessageBox::information(this, "删除成功", msg);
        requestFileList();  // 刷新文件列表
    } else {
        QMessageBox::warning(this, "删除失败", msg);
    }
}

// ========================== 历史记录相关函数 ==========================
void Widget::on_recordButton_clicked()
{
    // 检查对话框是否已存在
    if (m_historyDialog) {
        m_historyDialog->activateWindow();  // 激活已有窗口
        return;
    }

    // 创建新对话框（关闭时自动销毁）
    HistoryDialog* dialog = new HistoryDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_historyDialog = dialog;  // QPointer 自动管理对象生命周期

    // 连接历史记录刷新信号
    connect(dialog, &HistoryDialog::refreshHistoryRequested,
            this, &Widget::onHistoryRefreshRequested, Qt::UniqueConnection);

    dialog->show();
    sendHistoryRequest();  // 发送历史记录查询请求
}

void Widget::sendHistoryRequest()
{
    if (!socket || socket->state() != QTcpSocket::ConnectedState) {
        if (m_historyDialog) m_historyDialog->showStatus("未连接到服务器");
        return;
    }

    // 构建历史查询请求
    QJsonObject reqObj;
    reqObj["type"] = "history_query";
    reqObj["username"] = m_username;

    QByteArray jsonData = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);
    quint32 dataLen = static_cast<quint32>(jsonData.size());
    quint32 netLen = qToBigEndian(dataLen);  // 网络字节序

    QByteArray sendData;
    sendData.append(reinterpret_cast<const char*>(&netLen), 4);
    sendData.append(jsonData);
    socket->write(sendData);
}

void Widget::onHistoryRefreshRequested()
{
    sendHistoryRequest();  // 响应对话框的刷新请求
}
void Widget::handleHistoryResponse(const QJsonObject& json)
{
    qDebug() << "收到历史记录响应，成功状态：" << json["success"].toBool();
    bool success = json["success"].toBool();

    if (!m_historyDialog) {
        qDebug() << "历史对话框已关闭，忽略响应";
        return;
    }

    if (success) {
        m_historyRecords.clear();
        QJsonArray recordsArray = json["records"].toArray();

        // -------------------- 新增：定义系统根目录前缀 --------------------
        const QString systemRootPrefix = "/home/tmn/servertest";

        for (const QJsonValue &val : recordsArray) {
            QJsonObject recordObj = val.toObject();
            HistoryRecord record;
            QString rawFileName = recordObj["filename"].toString();

            // -------------------- 新增：去除系统根目录前缀 --------------------
            if (rawFileName.startsWith(systemRootPrefix)) {
                record.fileName = rawFileName.remove(0, systemRootPrefix.length());
            } else {
                record.fileName = rawFileName; // 若不匹配，保持原文件名（防止异常）
            }

            record.operationType = recordObj["operation"].toString();
            record.time = QDateTime::fromString(recordObj["time"].toString(), "yyyy-MM-dd hh:mm:ss");
            record.status = recordObj["status"].toString();
            m_historyRecords.append(record);
        }

        m_historyDialog->setHistoryRecords(m_historyRecords);
        m_historyDialog->showStatus(QString("获取成功，共 %1 条记录").arg(m_historyRecords.size()));
    } else {
        QString errMsg = json["message"].toString();
        m_historyDialog->showStatus("获取失败：" + errMsg);
    }
}

// 【辅助】转发历史记录响应到主线程（避免线程安全问题）
void Widget::handleHistoryResultMsg(const QJsonObject &json)
{
    // 用 QueuedConnection 确保在主线程更新界面
    QMetaObject::invokeMethod(this, "handleHistoryResponse",
                              Qt::QueuedConnection, Q_ARG(const QJsonObject&, json));
    qDebug()<<"转发历史记录响应到主线程（避免线程安全问题）"<<endl;
}

// ========================== 通用控制消息处理 ==========================
// 【辅助】处理进度更新消息（服务器主动推送的进度）
void Widget::handleProgressMsg(const QJsonObject &json)
{
    int percent = json["percent"].toInt();
    ui->progressBar->setValue(percent);

    // 区分上传/下载进度显示
    if (json["type"] == "upload_progress") {
        showStatus(QString("上传中：%1/%2 字节（%3%）")
                   .arg(uploadedSize).arg(totalUploadSize).arg(percent));
    } /*else if (json["type"] == "download_progress") {
        showStatus(QString("下载中：%1/%2 字节（%3%）")
                   .arg(downloadedSize).arg(totalDownloadSize).arg(percent));
    }*/
}

// ========================== 核心消息接收函数（主入口） ==========================
void Widget::on_readyRead()
{
    // ========== 关键修改：无论状态，先存数据到 recvBuffer ==========
    recvBuffer.append(socket->readAll());

    // 1. 下载阶段：优先处理二进制文件数据
    if (transferState == TransferState::Downloading) {
        qDebug() << "TransferState::Downloading" << endl;

        // ========== 新增：循环处理所有控制消息，直到无消息可处理 ==========
        bool hasProcessed;
        do {
            int beforeSize = recvBuffer.size();
            handleDownloadControlLogic(); // 处理控制消息（如download_progress）
            hasProcessed = (recvBuffer.size() < beforeSize); // 判断是否真的处理了消息
        } while (hasProcessed); // 循环直到没有控制消息可处理
        handleDownloadData();
        return;
    }

    // 2. 非下载阶段：处理JSON控制消息（上传/列表/删除等）
    if (transferState == TransferState::Idle ||
            transferState == TransferState::Uploading ||
            transferState == TransferState::WaitingDownloadMeta) {

        recvBuffer.append(socket->readAll());

        while (true) {
            // 步骤1：读取4字节长度前缀（网络字节序）
            if (recvBuffer.size() < 4) break;

            quint32 netLen;
            memcpy(&netLen, recvBuffer.data(), 4);
            quint32 dataLen = qFromBigEndian(netLen);  // 转为主机字节序

            // 步骤2：判断数据是否接收完整
            if (recvBuffer.size() < 4 + dataLen) break;

            // 步骤3：提取并解析JSON数据
            QByteArray jsonData = recvBuffer.mid(4, dataLen);
            recvBuffer.remove(0, 4 + dataLen);  // 移除已处理的数据

            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                qDebug() << "JSON解析失败：" << parseError.errorString() << "，原始数据：" << jsonData;
                continue;
            }

            QJsonObject json = doc.object();
            QString type = json["type"].toString();
            qDebug() << "[接收] 控制消息类型：" << type;

            // 步骤4：分发消息到对应处理函数
            if (type == "file_list") {
                handleFileListMsg(json);
            } else if (type == "history_result") {
                handleHistoryResultMsg(json);
            } else if (type == "ready_to_receive" && transferState == TransferState::Uploading) {
                handleReadyToReceiveMsg();
            } else if (type == "upload_result") {
                handleUploadResultMsg(json);
            } else if (type == "download_meta") {
                handleDownloadMetaMsg(json);
                // 切换状态后，若需处理剩余数据：先彻底清控制消息，再处理文件数据
                //if (transferState == TransferState::Downloading && !recvBuffer.isEmpty()) {
                // 1. 无论是否收到ready_to_send，都先彻底处理所有控制消息（循环处理，确保无遗漏）
                bool hasProcessed;
                do {
                    int beforeSize = recvBuffer.size();
                    handleDownloadControlLogic(); // 处理控制消息（含ready_to_send、download_progress等）
                    hasProcessed = (recvBuffer.size() < beforeSize); // 检查是否处理了消息
                } while (hasProcessed); // 循环处理，直到没有控制消息可处理

                // 2. 确认准备就绪后，处理剩余的纯文件数据
                if (isReadyToSendReceived && !recvBuffer.isEmpty()) {
                    handleDownloadData();
                }
                // 3. 移除break，避免中断后续可能的处理流程（如继续接收新数据）
                //}
            }else if (type == "delete_result") {
                handleDeleteResultMsg(json);
            } else if (type == "upload_resume_info") {
                handleUploadResumeMsg(json);
            } else if (type == "upload_progress"/* || type == "download_progress"*/) {
                handleProgressMsg(json);
            } else if (type == "share_request") {
                handleShareRequest(json);

            } else if (type == "pending_shares") {
                // 处理离线时收到的分享请求
                QJsonArray shares = json["shares"].toArray();
                for (auto share : shares) {
                    handleShareRequest(share.toObject());
                }
            } else if (type == "share_result") {
                bool success = json["success"].toBool();
                QString message = json["message"].toString();
                QMessageBox::information(this, "分享结果", message);
            }
            else {
                qDebug() << "[接收] 未知消息类型：" << type;
            }
        }
    }
}

// ========================== 其他功能函数 ==========================
void Widget::requestUploadResume(const QString& filepath)
{
    QJsonObject json;
    json["type"] = "check_upload_resume";
    json["filepath"] = filepath;
    sendJsonMessage(json);
}

void Widget::on_shareButton_clicked()
{
    QListWidgetItem *selectedItem = ui->fileListWidget->currentItem();
    if (!selectedItem) {
        QMessageBox::warning(this, "提示", "请先选择要分享的文件或目录");
        return;
    }

    QString fileName = selectedItem->text();
    QString actualName = fileName.endsWith("/") ? fileName.left(fileName.size() - 1) : fileName;

    // 创建分享对话框并设置放大后的尺寸
    shareDialog = new QDialog(this);
    shareDialog->setWindowTitle("分享文件");
    // 设置对话框大小（默认约400x200，放大一倍到800x400）
    shareDialog->setFixedSize(800, 400);

    // 创建主布局并增加间距
    QVBoxLayout *layout = new QVBoxLayout(shareDialog);
    layout->setSpacing(30); // 控件间距放大
    layout->setContentsMargins(40, 40, 40, 40); // 边距放大

    // 接收者输入区域
    QHBoxLayout *recipientLayout = new QHBoxLayout();
    recipientLayout->setSpacing(20); // 行内间距放大

    // 接收者标签（放大字体）
    QLabel *recipientLabel = new QLabel("接收者用户名:");
    QFont labelFont = recipientLabel->font();
    labelFont.setPointSize(14); // 字体放大
    recipientLabel->setFont(labelFont);
    recipientLabel->setFixedHeight(40); // 标签高度放大
    recipientLayout->addWidget(recipientLabel);

    // 接收者输入框（放大尺寸）
    recipientEdit = new QLineEdit();
    QFont editFont = recipientEdit->font();
    editFont.setPointSize(14);
    recipientEdit->setFont(editFont);
    recipientEdit->setFixedHeight(50); // 输入框高度放大
    recipientEdit->setMinimumWidth(400); // 输入框宽度放大
    recipientLayout->addWidget(recipientEdit);

    layout->addLayout(recipientLayout);

    // 按钮区域
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(30); // 按钮间距放大
    btnLayout->setAlignment(Qt::AlignRight); // 按钮靠右对齐

    // 取消按钮（放大尺寸）
    QPushButton *cancelBtn = new QPushButton("取消");
    cancelBtn->setFont(labelFont);
    cancelBtn->setFixedSize(120, 50); // 按钮尺寸放大
    btnLayout->addWidget(cancelBtn);

    // 确认按钮（放大尺寸）
    QPushButton *okBtn = new QPushButton("确认");
    okBtn->setFont(labelFont);
    okBtn->setFixedSize(120, 50); // 按钮尺寸放大
    btnLayout->addWidget(okBtn);

    layout->addLayout(btnLayout);

    // 连接信号槽
    connect(okBtn, &QPushButton::clicked, this, &Widget::on_shareDialogAccepted);
    connect(cancelBtn, &QPushButton::clicked, shareDialog, &QDialog::close);

    // 保存当前选中的文件名和路径
    shareDialog->setProperty("filename", actualName);
    shareDialog->setProperty("path", currentPath);

    shareDialog->exec();
}

void Widget::on_shareDialogAccepted()
{
    QString recipient = recipientEdit->text();
    QString filename = shareDialog->property("filename").toString();
    QString path = shareDialog->property("path").toString();

    if (recipient.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入接收者用户名");
        return;
    }

    // 发送分享请求
    QJsonObject json;
    json["type"] = "share";
    json["filename"] = filename;
    json["path"] = path;
    json["recipient"] = recipient;
    sendJsonMessage(json);

    showStatus("发送分享请求: " + filename + " 给 " + recipient);
    shareDialog->close();
}
void Widget::handleShareRequest(const QJsonObject &json)
{
    int shareId = json["id"].toInt();
    QString owner = json["owner"].toString();
    QString filename = json["filename"].toString();

    // 保存当前分享ID
    currentShareId = shareId;

    // 创建分享请求对话框
    shareRequestDialog = new QDialog(this);
    shareRequestDialog->setWindowTitle("收到文件分享");
    shareRequestDialog->setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(shareRequestDialog);

    QString message = QString("%1 向您分享了文件: %2\n是否接受?").arg(owner).arg(filename);
    layout->addWidget(new QLabel(message));

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *acceptBtn = new QPushButton("接受");
    QPushButton *rejectBtn = new QPushButton("拒绝");
    btnLayout->addWidget(acceptBtn);
    btnLayout->addWidget(rejectBtn);
    layout->addLayout(btnLayout);

    connect(acceptBtn, &QPushButton::clicked, this, &Widget::on_acceptShareClicked);
    connect(rejectBtn, &QPushButton::clicked, this, &Widget::on_rejectShareClicked);

    shareRequestDialog->exec();
}

void Widget::on_acceptShareClicked()
{
    // 发送接受分享的响应
    QJsonObject json;
    json["type"] = "share_response";
    json["share_id"] = currentShareId;
    json["action"] = "accept";
    sendJsonMessage(json);

    showStatus("已接受分享");
    shareRequestDialog->close();
}

void Widget::on_rejectShareClicked()
{
    // 发送拒绝分享的响应
    QJsonObject json;
    json["type"] = "share_response";
    json["share_id"] = currentShareId;
    json["action"] = "reject";
    sendJsonMessage(json);

    showStatus("已拒绝分享");
    shareRequestDialog->close();
}
void Widget::on_logoutButton_clicked()
{
    // 发送登出请求（可选，根据服务器协议）
    QJsonObject json;
    json["type"] = "logout";
    json["username"] = m_username;
    sendJsonMessage(json);

    // 断开连接并返回登录界面
    socket->disconnectFromHost();
    LoginWidget *loginWidget = new LoginWidget;
    loginWidget->show();
    this->close();
}

void Widget::on_disconnected()
{
    showStatus("与服务器断开连接");
    // 禁用界面操作
    ui->uploadButton->setEnabled(false);
    ui->downloadButton->setEnabled(false);
    ui->deleteButton->setEnabled(false);
    ui->refreshButton->setEnabled(false);
    ui->backButton->setEnabled(false);
    ui->shareButton->setEnabled(false);
    // 重置传输状态
    transferState = TransferState::Idle;
}

void Widget::on_errorOccurred(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QMessageBox::critical(this, "网络错误", "连接异常：" + socket->errorString());
    showStatus("错误：" + socket->errorString());
}
