#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>

#include "MainWindow.h"

namespace {

QString singleInstanceServerName() {
  const QByteArray userId = QCryptographicHash::hash(
      QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
  return QStringLiteral("ws2tcp-local-qt-%1").arg(QString::fromLatin1(userId));
}

bool notifyRunningInstance(const QString &serverName, int timeoutMs = 500) {
  QLocalSocket socket;
  socket.connectToServer(serverName, QIODevice::WriteOnly);
  if (!socket.waitForConnected(timeoutMs)) {
    return false;
  }

  socket.write("activate");
  socket.flush();
  socket.waitForBytesWritten(timeoutMs);
  return true;
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("ws2tcp-local");
  QCoreApplication::setApplicationName("ws2tcp-local-qt");
  QApplication::setQuitOnLastWindowClosed(false);

  const QString serverName = singleInstanceServerName();
  if (notifyRunningInstance(serverName)) {
    return 0;
  }

  QLocalServer instanceServer;
  instanceServer.setSocketOptions(QLocalServer::UserAccessOption);
  if (!instanceServer.listen(serverName)) {
    // Another process may have won the startup race after our first check.
    if (notifyRunningInstance(serverName, 1000)) {
      return 0;
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
      socket->deleteLater();
      window.showAndActivate();
    }
  });

  return app.exec();
}
