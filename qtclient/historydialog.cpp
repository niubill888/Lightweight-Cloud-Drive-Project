#include "historydialog.h"
#include "ui_historydialog.h"
#include <QDebug>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QThread>
#include <QCoreApplication>

HistoryDialog::HistoryDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::HistoryDialog),
    m_isDeleted(false)  // 初始化删除标记
{
    ui->setupUi(this);
    setWindowTitle("操作历史记录");
    setAttribute(Qt::WA_DeleteOnClose);  // 关闭时自动销毁

    // 初始化表格
    ui->historyTableWidget->setColumnCount(4);
    ui->historyTableWidget->setHorizontalHeaderLabels({"文件名", "操作类型", "操作时间", "状态"});
    ui->historyTableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->historyTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->historyTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui->historyTableWidget->setColumnWidth(0, 200);
    ui->historyTableWidget->setColumnWidth(1, 140);
    ui->historyTableWidget->setColumnWidth(2, 260);
    ui->historyTableWidget->setColumnWidth(3, 100);

    // 检查状态条是否存在
    if (!ui->statusBar) {
        qCritical() << "错误：UI文件中未添加QStatusBar控件！";
    }
}

HistoryDialog::~HistoryDialog()
{
    m_isDeleted = true;  // 标记为已删除
    cancelPendingEvents();  // 清除未处理事件
    disconnect();           // 断开所有信号连接
    ui->historyTableWidget->setRowCount(0);
    delete ui;
    qDebug() << "HistoryDialog 已销毁";
}

void HistoryDialog::setHistoryRecords(const QList<HistoryRecord>& records)
{
    // 检查对象是否已被标记为删除（Qt5.12兼容写法）
    if (m_isDeleted) {
        qDebug() << "忽略对已销毁对话框的操作";
        return;
    }

    // 跨线程调用处理
    if (QThread::currentThread() != thread()) {
        // 用lambda包装，增加安全性检查
        QMetaObject::invokeMethod(this, [this, records]() {
            if (!m_isDeleted) {  // 再次检查
                setHistoryRecords(records);
            }
        }, Qt::QueuedConnection);
        return;
    }

    // 表格更新优化
    ui->historyTableWidget->setUpdatesEnabled(false);
    ui->historyTableWidget->clearContents();
    ui->historyTableWidget->setRowCount(records.size());

    // 填充数据
    for (int i = 0; i < records.size(); ++i) {
        const HistoryRecord& record = records[i];
        ui->historyTableWidget->setItem(i, 0, new QTableWidgetItem(record.fileName));
        ui->historyTableWidget->setItem(i, 1, new QTableWidgetItem(record.operationType));
        ui->historyTableWidget->setItem(i, 2, new QTableWidgetItem(record.time.toString("yyyy-MM-dd hh:mm:ss")));

        QTableWidgetItem* statusItem = new QTableWidgetItem(record.status);
        statusItem->setTextColor(record.status == "成功" ? Qt::green : Qt::red);
        ui->historyTableWidget->setItem(i, 3, statusItem);
    }

    ui->historyTableWidget->setUpdatesEnabled(true);

    if (ui->statusBar) {
        ui->statusBar->showMessage(QString("已刷新，共%1条记录").arg(records.size()), 2000);
    }
}

void HistoryDialog::showStatus(const QString& msg)
{
    if (ui->statusBar && !m_isDeleted) {  // 增加删除检查
        ui->statusBar->showMessage(msg);
    }
}


void HistoryDialog::on_refreshButton_clicked()
{
    if (m_isDeleted) return;  // 增加删除检查

    if (ui->statusBar) {
        ui->statusBar->showMessage("正在获取历史记录...");
    }
    emit refreshHistoryRequested();
}

void HistoryDialog::on_closeButton_clicked()
{
    close();
}

void HistoryDialog::cancelPendingEvents()
{
    // 清除所有未处理的事件
    QCoreApplication::removePostedEvents(this);
}
