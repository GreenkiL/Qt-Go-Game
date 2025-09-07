QT += widgets network websockets
QT += widgets
CONFIG += c++11
TARGET = goqt
TEMPLATE = app

SOURCES += \
    ai_random.cpp \
    boardwidget.cpp \
    gamewindow.cpp \
    goban.cpp \
    lobbywindow.cpp \
    loginwindow.cpp \
    main.cpp \
    networkmanager.cpp \
    singleplayer.cpp

HEADERS += \
    ai_random.h \
    boardwidget.h \
    gamewindow.h \
    goban.h \
    lobbywindow.h \
    loginwindow.h \
    networkmanager.h \
    singleplayer.h

# adjust if using msys/mingw: uncomment
# QMAKE_LFLAGS += -static
