#ifndef WINDOWSSYSTEMPROXY_H
#define WINDOWSSYSTEMPROXY_H

#include <QString>

class WindowsSystemProxy final {
 public:
  static bool enable(const QString &listenAddress, QString *error);
  static bool disable(QString *error);
  static void recoverStaleSettings();

 private:
  WindowsSystemProxy() = delete;
};

#endif
