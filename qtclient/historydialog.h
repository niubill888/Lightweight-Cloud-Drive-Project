#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>
#include <QList>
#include <QDateTime>

namespace Ui {
class HistoryDialog;
}

// 历史记录结构体
struct HistoryRecord {
    QString fileName;       // 文件名
    QString operationType;  // 操作类型（上传/下载/删除等）
    QDateTime time;         // 操作时间
    QString status;         // 操作状态（成功/失败）
};

class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(QWidget *parent = nullptr);
    ~HistoryDialog();

    // 设置历史记录数据
    void setHistoryRecords(const QList<HistoryRecord>& records);
    // 显示状态信息
    void showStatus(const QString& msg);

signals:
    // 刷新历史记录请求
    void refreshHistoryRequested();

private slots:
    // 刷新按钮点击事件
    void on_refreshButton_clicked();
    // 关闭按钮点击事件
    void on_closeButton_clicked();
    // 取消所有未处理事件
    void cancelPendingEvents();

private:
    Ui::HistoryDialog *ui;
    bool m_isDeleted;  // 用于标记对象是否已删除
};

#endif // HISTORYDIALOG_H
