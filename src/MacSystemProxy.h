#ifndef MACSYSTEMPROXY_H
#define MACSYSTEMPROXY_H

#include <QString>

class MacSystemProxy final {
 public:
  static bool enable(const QString &listenAddress, QString *error);
  static bool disable(QString *error);
  static void recoverStaleSettings();

 private:
  MacSystemProxy() = delete;
};

#endif
