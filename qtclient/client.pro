QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = CloudClient
TEMPLATE = app

SOURCES += \
    historydialog.cpp \
    main.cpp \
    widget.cpp \
    loginwidget.cpp \

HEADERS += \
    historydialog.h \
    widget.h \
    loginwidget.h \

FORMS += \
    historydialog.ui \
    widget.ui \
    loginwidget.ui \

# Windows平台网络库
win32: LIBS += -lws2_32
