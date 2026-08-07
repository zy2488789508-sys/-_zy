QT += core widgets charts printsupport
CONFIG += c++11

TARGET = sensor_display
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h \
    shared_data.h

LIBS += -lrt -lm
