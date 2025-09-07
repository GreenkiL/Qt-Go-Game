QT += core network websockets sql
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app

SOURCES += main.cpp \
           AuthManager.cpp \
           GameServer.cpp

HEADERS += GameServer.h \
    AuthManager.h
