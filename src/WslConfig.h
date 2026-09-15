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

  // True only if .wslconfig exists and its [wsl2] section sets
  // networkingMode=mirrored. Anything else (missing file, missing section
  // or key, or a different value) is reported as false.
  static bool isMirroredNetworkingEnabled();

 private:
  WslConfig() = delete;
};

#endif
