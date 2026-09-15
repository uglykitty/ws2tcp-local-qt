#ifndef WSLCONFIG_H
#define WSLCONFIG_H

#include <QString>

class WslConfig final {
 public:
  // Sets `networkingMode=mirrored` under the [wsl2] section of the current
  // user's %USERPROFILE%\.wslconfig, creating the file or the section if
  // needed. All other content, including comments and unrelated sections,
  // is preserved verbatim.
  static bool enableMirroredNetworking(QString *error);

 private:
  WslConfig() = delete;
};

#endif
