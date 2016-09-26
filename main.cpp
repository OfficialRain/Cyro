#include "mainwindow.h"
#include "background.h"
#include "loginsplash.h"
#include "segfaultdialog.h"
#include "globalfilter.h"
#include "dbusevents.h"
#include <iostream>
//#include "dbusmenuregistrar.h"
#include <nativeeventfilter.h>
#include <QApplication>
#include <QDesktopWidget>
#include <QProcess>
#include <QThread>
#include <QUrl>
#include <QMediaPlayer>
#include <QDebug>
#include <QSettings>
#include <QInputDialog>
#include <signal.h>
#include <unistd.h>

MainWindow* MainWin = NULL;
NativeEventFilter* NativeFilter = NULL;
DbusEvents* DBusEvents = NULL;

void raise_signal(QString message) {
    //Clean up required stuff

    //Delete the Native Event Filter so that keyboard bindings are cleared
    if (NativeFilter != NULL) {
        delete NativeFilter;
        NativeFilter = NULL;
    }

    SegfaultDialog* dialog;
    dialog = new SegfaultDialog(message);
    if (MainWin != NULL) {
        MainWin->close();
        delete MainWin;
    }
    dialog->exec();
    raise(SIGKILL);
}

void catch_signal(int signal) {
    if (signal == SIGSEGV) {
        qDebug() << "SEGFAULT! Quitting now!";
        raise_signal("Signal: SIGSEGV (Segmentation Fault)");
    } else if (signal == SIGBUS) {
        qDebug() << "SIGBUS! Quitting now!";
        raise_signal("Signal: SIGBUS (Bus Error)");
    } else if (signal == SIGABRT) {
        qDebug() << "SIGABRT! Quitting now!";
        raise_signal("Signal: SIGABRT (Abort)");
    } else if (signal == SIGILL) {
        qDebug() << "SIGILL! Quitting now!";
        raise_signal("Signal: SIGILL (Illegal Operation)");
    }
}


void QtHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    switch (type) {
    case QtDebugMsg:
    case QtInfoMsg:
    case QtWarningMsg:
    case QtCriticalMsg:
        std::cout << msg.toStdString();
        break;
    case QtFatalMsg:
        std::cout << msg.toStdString();
        raise_signal(msg);
    }
}

int main(int argc, char *argv[])
{
    signal(SIGSEGV, *catch_signal); //Catch SIGSEGV
    signal(SIGBUS, *catch_signal); //Catch SIGBUS
    signal(SIGABRT, *catch_signal); //Catch SIGABRT
    signal(SIGILL, *catch_signal); //Catch SIGILL

    qInstallMessageHandler(QtHandler);

    QApplication a(argc, argv);

    bool showSplash = true;
    bool autoStart = true;
    bool startKscreen = true;

    QStringList args = a.arguments();
    args.removeFirst();
    for (QString arg : a.arguments()) {
        if (arg == "--help" || arg == "-h") {
            qDebug() << "theShell";
            qDebug() << "Usage: theshell [OPTIONS]";
            qDebug() << "  -s, --no-splash-screen       Don't show the splash screen";
            qDebug() << "  -a, --no-autostart           Don't autostart executables";
            qDebug() << "  -k, --no-kscreen             Don't autostart KScreen";
            qDebug() << "      --debug                  Allows you to quit theShell instead of powering off";
            qDebug() << "  -h, --help                   Show this help output";
            return 0;
        } else if (arg == "-s" || arg == "--no-splash-screen") {
            showSplash = false;
        } else if (arg == "-a" || arg == "--no-autostart") {
            autoStart = false;
        } else if (arg == "-k" || arg == "--no-kscreen") {
            startKscreen = false;
        }
    }

    a.setOrganizationName("theSuite");
    a.setOrganizationDomain("");
    a.setApplicationName("theShell");

    QSettings settings;

    if (QDBusConnection::sessionBus().interface()->registeredServiceNames().value().contains("org.thesuite.theshell")) {
        if (QMessageBox::warning(0, "theShell already running", "theShell seems to already be running. "
                                                               "Do you wish to start theShell anyway?",
                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
            return 0;
        }
    }

    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerService("org.thesuite.theshell");

    if (startKscreen) {
        QDBusMessage kscreen = QDBusMessage::createMethodCall("org.kde.kded5", "/kded", "org.kde.kded5", "loadModule");
        QVariantList args;
        args.append("kscreen");
        kscreen.setArguments(args);
        QDBusConnection::sessionBus().call(kscreen);
    }

    QString windowManager = settings.value("startup/WindowManagerCommand", "kwin_x11").toString();

    while (!QProcess::startDetached(windowManager)) {
        windowManager = QInputDialog::getText(0, "Window Manager couldn't start",
                              "The window manager \"" + windowManager + "\" could not start. \n\n"
                              "Enter the name or path of a window manager to attempt to start a different window"
                              "manager, or hit 'Cancel' to start theShell without a window manager.");
        if (windowManager == "") {
            break;
        }
    }

    if (showSplash) {
        LoginSplash* splash = new LoginSplash();
        splash->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        splash->showFullScreen();
    }

    if (!QDBusConnection::sessionBus().interface()->registeredServiceNames().value().contains("org.kde.kdeconnect") && QFile("/usr/lib/kdeconnectd").exists()) {
        //Start KDE Connect if it is not running and it is existant on the PC
        QProcess::startDetached("/usr/lib/kdeconnectd");
    }

    QProcess polkitProcess;
    polkitProcess.start("/usr/lib/ts-polkitagent");

    QProcess btProcess;
    btProcess.start("ts-bt");
    btProcess.waitForStarted(); //Wait for ts-bt to start so that the Bluetooth toggle will work properly

    NativeFilter = new NativeEventFilter();
    a.installNativeEventFilter(NativeFilter);

    MainWin = new MainWindow();

    new GlobalFilter(&a);

    DBusEvents = new DbusEvents();

    qDBusRegisterMetaType<QList<QVariantMap>>();
    qDBusRegisterMetaType<QMap<QString, QVariantMap>>();


    if (autoStart) {
        QStringList autostartApps = settings.value("startup/autostart", "").toString().split(",");
        for (QString app : autostartApps) {
            QProcess::startDetached(app);
        }
    }

    QRect screenGeometry = QApplication::desktop()->screenGeometry();

    MainWin->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    MainWin->setGeometry(screenGeometry.x() - 1, screenGeometry.y(), screenGeometry.width() + 1, MainWin->height());
    MainWin->show();

    QThread::sleep(1);

    return a.exec();
}

void playSound(QUrl location, bool uncompressed = false) {
    if (uncompressed) {
        QSoundEffect* sound = new QSoundEffect();
        sound->setSource(location);
        sound->play();
    } else {
        QMediaPlayer* sound = new QMediaPlayer();
        sound->setMedia(location);
        sound->play();
    }
}

QIcon getIconFromTheme(QString name, QColor textColor) {
    int averageCol = (textColor.red() + textColor.green() + textColor.blue()) / 3;

    if (averageCol <= 127) {
        return QIcon(":/icons/dark/images/dark/" + name);
    } else {
        return QIcon(":/icons/light/images/light/" + name);
    }
}

void EndSession(EndSessionWait::shutdownType type) {
    switch (type) {
    case EndSessionWait::powerOff:
    case EndSessionWait::reboot:
    case EndSessionWait::logout:
    case EndSessionWait::dummy:
    {
        EndSessionWait* w = new EndSessionWait(type);
        w->showFullScreen();
        QApplication::setOverrideCursor(Qt::BlankCursor);
    }
        break;

    case EndSessionWait::suspend:
    {
        QList<QVariant> arguments;
        arguments.append(true);

        QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Suspend");
        message.setArguments(arguments);
        QDBusConnection::systemBus().send(message);
    }
        break;
    case EndSessionWait::hibernate:
    {

        QList<QVariant> arguments;
        arguments.append(true);

        QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Hibernate");
        message.setArguments(arguments);
        QDBusConnection::systemBus().send(message);
    }
        break;
    }

}

QString calculateSize(quint64 size) {
    QString ret;
    if (size > 1073741824) {
        ret = QString::number(((float) size / 1024 / 1024 / 1024), 'f', 2).append(" GiB");
    } else if (size > 1048576) {
        ret = QString::number(((float) size / 1024 / 1024), 'f', 2).append(" MiB");
    } else if (size > 1024) {
        ret = QString::number(((float) size / 1024), 'f', 2).append(" KiB");
    } else {
        ret = QString::number((float) size, 'f', 2).append(" B");
    }

    return ret;
}
