include(../common_top.pri)
TARGET = ut_desktopview
INCLUDEPATH += $$SRCDIR/notifications

MODEL_HEADERS += $$SRCDIR/desktopmodel.h
STYLE_HEADERS += $$SRCDIR/desktopstyle.h

# unit test and unit
SOURCES += \
    ut_desktopview.cpp \
    $$SRCDIR/desktopview.cpp \
    $$SRCDIR/windowinfo.cpp

# base classes
SOURCES += \


# unit test and unit
HEADERS += \
    ut_desktopview.h \
    $$SRCDIR/desktopview.h

# base classes
HEADERS += \

# service classes
SOURCES += ../stubs/stubbase.cpp

# service classes
HEADERS += \
    $$SRCDIR/windowinfo.h \
    $$SRCDIR/desktop.h \
    $$SRCDIR/desktopmodel.h \
    $$SRCDIR/desktopstyle.h \
    $$SRCDIR/mainwindow.h \
    $$SRCDIR/switcher.h \
    $$SRCDIR/quicklaunchbar.h \
    $$SRCDIR/launcher.h \
    $$SRCDIR/launcherdatastore.h \
    $$SRCDIR/applicationpackagemonitor.h

DEFINES += APPLICATIONS_DIRECTORY=\'$$quote(\"/tmp/ut_desktopview/applications/\")\'

include(../common_bot.pri)
