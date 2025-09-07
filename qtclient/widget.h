#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTcpSocket>
#include <QPointer>
#include <QList>
#include <QByteArray>
#include <QDateTime>
#include <historydialog.h>
#include <QLineEdit>
#include <QComboBox>
#include <QHBoxLayout>

// 前向声明
class QTcpSocket;
class QListWidgetItem;
class HistoryDialog;

// 文件信息结构体
struct FileInfo {
    QString name;
    bool isDirectory;
    FileInfo(QString n, bool dir) : name(n), isDirectory(dir) {}
};


// 传输状态枚举
enum class TransferState {
    Idle,          // 空闲
    Uploading,     // 上传中
    WaitingDownloadMeta,  // 等待下载元信息
    Downloading    // 下载中
};

namespace Ui {
class Widget;
}

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QTcpSocket *socket, const QString &username, QWidget *parent = nullptr);
    ~Widget();

    static const int BUFFER_SIZE;  // 缓冲区大小（4096）

    QByteArray getRecvBuffer() const { return recvBuffer; }
    void setRecvBuffer(const QByteArray &buf) { recvBuffer = buf; }

private slots:
    // 界面按钮槽函数
    void on_uploadButton_clicked();
    void on_downloadButton_clicked();
    void on_deleteButton_clicked();
    void on_refreshButton_clicked();
    void on_backButton_clicked();
    void on_recordButton_clicked();
    void on_shareButton_clicked();
    void on_logoutButton_clicked();

    // 网络相关槽函数
    void on_readyRead();
    void on_disconnected();
    void on_errorOccurred(QAbstractSocket::SocketError error);
    void onBytesWritten(qint64 bytes);  // 新增：异步上传驱动

    // 界面交互槽函数
    void onFileListDoubleClicked(QListWidgetItem *item);
    void onHistoryRefreshRequested();  // 历史记录刷新请求

    void on_shareDialogAccepted();
    void on_acceptShareClicked();
    void on_rejectShareClicked();

private:
    // 基础工具函数
    void sendJsonMessage(const QJsonObject &json);
    void showStatus(const QString &msg);
    void requestFileList();
    void updatePathDisplay();

    // 上传相关函数
    void cleanupUpload();
    void handleReadyToReceiveMsg();
    void sendNextUploadData();  // 新增：发送下一批上传数据
    void handleUploadResultMsg(const QJsonObject &json);
    void handleUploadResumeMsg(const QJsonObject &json);
    void requestUploadResume(const QString& filepath);


    // 下载相关函数
    void cleanupDownload();
    void handleDownloadData();
    void handleDownloadMetaMsg(const QJsonObject &json);
    void handleDownloadControlLogic();

    // 文件管理相关函数
    void handleFileListMsg(const QJsonObject &json);
    void handleDeleteResultMsg(const QJsonObject &json);

    // 历史记录相关函数
    void sendHistoryRequest();
    Q_INVOKABLE void handleHistoryResponse(const QJsonObject& json);
    void handleHistoryResultMsg(const QJsonObject &json);

    // 通用控制函数
    void handleProgressMsg(const QJsonObject &json);

    QDialog *shareDialog;
    QLineEdit *recipientEdit;
    QDialog *shareRequestDialog;
    int currentShareId;  // 当前处理的分享ID
    void handleShareRequest(const QJsonObject &json);

private:
    Ui::Widget *ui;
    QTcpSocket *socket;
    QString currentPath;
    QString m_username;

    // 传输相关变量
    TransferState transferState;
    QFile *uploadFile;
    QFile *downloadFile;
    qint64 uploadedSize;
    qint64 totalUploadSize;
    qint64 downloadedSize;
    qint64 totalDownloadSize;
    QByteArray uploadBuffer;
    QByteArray recvBuffer;
    QString downloadFileName;
    bool isReadyToSendReceived;

    // 数据存储
    QList<FileInfo> fileList;
    QList<HistoryRecord> m_historyRecords;
    QPointer<HistoryDialog> m_historyDialog;  // 历史对话框（自动管理生命周期）
};

#endif // WIDGET_H
