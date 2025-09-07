#include "loginwidget.h"
#include "widget.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 显示登录界面
    LoginWidget loginWidget;
    loginWidget.show();

    return a.exec();
}
