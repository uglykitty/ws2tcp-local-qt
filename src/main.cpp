#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSettings>
#include <QThread>

#include "MainWindow.h"
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
#include "SystemProxy.h"
#endif

namespace {

QString singleInstanceServerName() {
  const QByteArray userId = QCryptographicHash::hash(
      QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
  return QStringLiteral("ws2tcp-local-qt-%1").arg(QString::fromLatin1(userId));
}

enum class NotificationResult { NoInstance, Succeeded, Failed };

NotificationResult notifyRunningInstance(const QString &serverName,
                                         const QByteArray &command,
                                         int timeoutMs = 500) {
  QLocalSocket socket;
  socket.connectToServer(serverName, QIODevice::ReadWrite);
  if (!socket.waitForConnected(timeoutMs)) {
    return NotificationResult::NoInstance;
  }

  socket.write(command);
  socket.flush();
  if (!socket.waitForBytesWritten(timeoutMs)) {
    return NotificationResult::Failed;
  }

  if (command != "clear-user-settings") {
    return NotificationResult::Succeeded;
  }

  if (!socket.waitForReadyRead(5000) || socket.readAll() != "ok") {
    return NotificationResult::Failed;
  }

  // The uninstaller must not start deleting files until the primary process
  // has actually released its single-instance server and exited.
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 5000) {
    QLocalSocket probe;
    probe.connectToServer(serverName, QIODevice::WriteOnly);
    if (!probe.waitForConnected(100)) {
      return NotificationResult::Succeeded;
    }
    probe.disconnectFromServer();
    QThread::msleep(50);
  }
  return NotificationResult::Failed;
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("ws2tcp-local");
  QCoreApplication::setApplicationName("ws2tcp-local-qt");
  QCoreApplication::setApplicationVersion(WS2TCP_LOCAL_VERSION);
  QApplication::setQuitOnLastWindowClosed(false);

  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Qt GUI for the ws2tcp-local WebSocket-to-TCP proxy");
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption clearUserSettingsOption(
      "clear-user-settings",
      "Clear the current user's saved settings and exit.");
  parser.addOption(clearUserSettingsOption);
  parser.process(app);

  const bool clearUserSettings =
      parser.isSet(clearUserSettingsOption);
  const QByteArray command =
      clearUserSettings ? QByteArrayLiteral("clear-user-settings")
                        : QByteArrayLiteral("activate");

  const QString serverName = singleInstanceServerName();
  const NotificationResult notification =
      notifyRunningInstance(serverName, command);
  if (notification == NotificationResult::Succeeded) {
    return 0;
  }
  if (notification == NotificationResult::Failed) {
    return 1;
  }

  if (clearUserSettings) {
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
    QString error;
    if (!SystemProxy::disable(&error)) {
      QMessageBox::critical(
          nullptr, "ws2tcp-local",
          "Unable to restore the system proxy: " + error);
      return 1;
    }
#endif
    QSettings settings;
    settings.clear();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
      QMessageBox::critical(nullptr, "ws2tcp-local",
                            "Unable to clear user settings");
      return 1;
    }
    return 0;
  }

  QLocalServer instanceServer;
  instanceServer.setSocketOptions(QLocalServer::UserAccessOption);
  if (!instanceServer.listen(serverName)) {
    // Another process may have won the startup race after our first check.
    const NotificationResult retryNotification =
        notifyRunningInstance(serverName, command, 1000);
    if (retryNotification == NotificationResult::Succeeded) {
      return 0;
    }
    if (retryNotification == NotificationResult::Failed) {
      return 1;
    }

    // A crashed process can leave a stale Unix-domain socket behind.
    QLocalServer::removeServer(serverName);
    if (!instanceServer.listen(serverName)) {
      QMessageBox::critical(
          nullptr, "ws2tcp-local",
          QString("Unable to create the single-instance service: %1")
              .arg(instanceServer.errorString()));
      return 1;
    }
  }

  MainWindow window;
  window.show();

  QObject::connect(&instanceServer, &QLocalServer::newConnection, &window,
                   [&instanceServer, &window]() {
    while (QLocalSocket *socket = instanceServer.nextPendingConnection()) {
      QObject::connect(socket, &QLocalSocket::readyRead, &window,
                       [socket, &window]() {
        const QByteArray command = socket->readAll();
        if (command == "clear-user-settings") {
          socket->write(window.clearUserSettingsAndQuit() ? "ok" : "error");
          socket->flush();
          socket->waitForBytesWritten(1000);
        } else {
          window.showAndActivate();
        }
        socket->disconnectFromServer();
      });
      QObject::connect(socket, &QLocalSocket::disconnected,
                       socket, &QObject::deleteLater);
    }
  });

  return app.exec();
}
