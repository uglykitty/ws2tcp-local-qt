#include "WindowsSystemProxy.h"

#include <QByteArray>
#include <QSettings>

#include <cstring>
#include <windows.h>
#include <wininet.h>

namespace {

constexpr wchar_t kInternetSettingsKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
constexpr wchar_t kEnvironmentKey[] = L"Environment";
constexpr char kSettingsGroup[] = "system_proxy/windows";

// Windows environment variable names are case-insensitive, so e.g.
// "HTTP_PROXY" and "http_proxy" would collide into a single value if both
// were listed here; only the canonical upper-case forms are meaningful.
constexpr const wchar_t *kProxyEnvNames[] = {
    L"HTTP_PROXY",
    L"HTTPS_PROXY",
    L"ALL_PROXY",
};
constexpr int kProxyEnvNameCount =
    static_cast<int>(sizeof(kProxyEnvNames) / sizeof(kProxyEnvNames[0]));

QString proxyEnvSettingsKey(int index) {
  return QString("original_env_%1").arg(index);
}

struct RegistryValue {
  bool exists = false;
  DWORD type = REG_NONE;
  QByteArray data;
};

QString windowsError(const QString &operation, LONG code) {
  return QString("%1 (Windows error %2)").arg(operation).arg(code);
}

bool readValue(HKEY key, const wchar_t *name, RegistryValue *value,
               QString *error) {
  DWORD type = REG_NONE;
  DWORD size = 0;
  LONG rc = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
  if (rc == ERROR_FILE_NOT_FOUND) {
    *value = {};
    return true;
  }
  if (rc != ERROR_SUCCESS) {
    if (error != nullptr) {
      *error = windowsError("Failed to read system proxy settings", rc);
    }
    return false;
  }

  QByteArray data(static_cast<qsizetype>(size), Qt::Uninitialized);
  rc = RegQueryValueExW(key, name, nullptr, &type,
                        reinterpret_cast<BYTE *>(data.data()), &size);
  if (rc != ERROR_SUCCESS) {
    if (error != nullptr) {
      *error = windowsError("Failed to read system proxy settings", rc);
    }
    return false;
  }

  value->exists = true;
  value->type = type;
  value->data = data;
  return true;
}

bool writeValue(HKEY key, const wchar_t *name, const RegistryValue &value,
                QString *error) {
  LONG rc = ERROR_SUCCESS;
  if (value.exists) {
    rc = RegSetValueExW(key, name, 0, value.type,
                        reinterpret_cast<const BYTE *>(value.data.constData()),
                        static_cast<DWORD>(value.data.size()));
  } else {
    rc = RegDeleteValueW(key, name);
    if (rc == ERROR_FILE_NOT_FOUND) {
      rc = ERROR_SUCCESS;
    }
  }

  if (rc != ERROR_SUCCESS) {
    if (error != nullptr) {
      *error = windowsError("Failed to write system proxy settings", rc);
    }
    return false;
  }
  return true;
}

RegistryValue dwordValue(DWORD number) {
  RegistryValue value;
  value.exists = true;
  value.type = REG_DWORD;
  value.data = QByteArray(reinterpret_cast<const char *>(&number),
                          sizeof(number));
  return value;
}

RegistryValue stringValue(const QString &text) {
  RegistryValue value;
  value.exists = true;
  value.type = REG_SZ;
  const qsizetype bytes = (text.size() + 1) * sizeof(wchar_t);
  value.data = QByteArray(reinterpret_cast<const char *>(text.utf16()), bytes);
  return value;
}

QString registryString(const RegistryValue &value) {
  if (!value.exists ||
      (value.type != REG_SZ && value.type != REG_EXPAND_SZ) ||
      value.data.size() < static_cast<qsizetype>(sizeof(wchar_t))) {
    return {};
  }
  return QString::fromWCharArray(
      reinterpret_cast<const wchar_t *>(value.data.constData()));
}

DWORD registryDword(const RegistryValue &value) {
  if (!value.exists || value.type != REG_DWORD ||
      value.data.size() != static_cast<qsizetype>(sizeof(DWORD))) {
    return 0;
  }
  DWORD number = 0;
  memcpy(&number, value.data.constData(), sizeof(number));
  return number;
}

void saveValue(QSettings &settings, const QString &name,
               const RegistryValue &value) {
  settings.setValue(name + "/exists", value.exists);
  settings.setValue(name + "/type", static_cast<uint>(value.type));
  settings.setValue(name + "/data", value.data);
}

RegistryValue loadValue(const QSettings &settings, const QString &name) {
  RegistryValue value;
  value.exists = settings.value(name + "/exists").toBool();
  value.type = settings.value(name + "/type").toUInt();
  value.data = settings.value(name + "/data").toByteArray();
  return value;
}

void notifyProxyChanged() {
  InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
  InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

void notifyEnvironmentChanged() {
  DWORD_PTR result = 0;
  SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                      reinterpret_cast<LPARAM>(L"Environment"),
                      SMTO_ABORTIFHUNG, 5000, &result);
}

QString normalizeListenAddress(const QString &listenAddress, QString *error) {
  const QString input = listenAddress.trimmed();
  const int separator = input.lastIndexOf(':');
  bool portOk = false;
  const int port = input.mid(separator + 1).toInt(&portOk);
  if (separator <= 0 || !portOk || port < 1 || port > 65535) {
    if (error != nullptr) {
      *error = "Listen address must contain a valid host and port";
    }
    return {};
  }

  QString host = input.left(separator).trimmed();
  if (host.startsWith('[') && host.endsWith(']')) {
    host = host.mid(1, host.size() - 2);
  }
  if (host == "0.0.0.0" || host == "::" || host == "*") {
    host = "127.0.0.1";
  }
  if (host.isEmpty()) {
    if (error != nullptr) {
      *error = "Listen address must contain a host";
    }
    return {};
  }

  if (host.contains(':')) {
    host = '[' + host + ']';
  }
  return QString("%1:%2").arg(host).arg(port);
}

bool restoreSavedSettings(QString *error, bool onlyIfOwned) {
  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  if (!settings.value("active", false).toBool()) {
    settings.endGroup();
    return true;
  }

  HKEY key = nullptr;
  LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0,
                          KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
  if (rc != ERROR_SUCCESS) {
    settings.endGroup();
    if (error != nullptr) {
      *error = windowsError("Failed to open system proxy settings", rc);
    }
    return false;
  }

  RegistryValue currentEnable;
  RegistryValue currentServer;
  QString readError;
  const bool readOk = readValue(key, L"ProxyEnable", &currentEnable,
                                &readError) &&
                      readValue(key, L"ProxyServer", &currentServer,
                                &readError);
  const QString appliedServer = settings.value("applied_server").toString();
  const bool stillOwned = readOk && registryDword(currentEnable) == 1 &&
                          registryString(currentServer) == appliedServer;

  bool restored = readOk;
  if (restored && (!onlyIfOwned || stillOwned)) {
    restored = writeValue(key, L"ProxyServer",
                          loadValue(settings, "original_server"), error) &&
               writeValue(key, L"ProxyEnable",
                          loadValue(settings, "original_enable"), error);

    HKEY envKey = nullptr;
    const LONG envRc = RegOpenKeyExW(HKEY_CURRENT_USER, kEnvironmentKey, 0,
                                     KEY_QUERY_VALUE | KEY_SET_VALUE, &envKey);
    if (envRc == ERROR_SUCCESS) {
      for (int i = 0; i < kProxyEnvNameCount; ++i) {
        if (!writeValue(envKey, kProxyEnvNames[i],
                        loadValue(settings, proxyEnvSettingsKey(i)), error)) {
          restored = false;
        }
      }
      RegCloseKey(envKey);
    } else {
      restored = false;
      if (error != nullptr && error->isEmpty()) {
        *error = windowsError("Failed to open environment settings", envRc);
      }
    }

    if (restored) {
      notifyProxyChanged();
      notifyEnvironmentChanged();
    }
  }
  RegCloseKey(key);

  if (restored) {
    settings.remove("");
  } else if (error != nullptr && error->isEmpty()) {
    *error = readError;
  }
  settings.endGroup();
  return restored;
}

}  // namespace

bool WindowsSystemProxy::enable(const QString &listenAddress, QString *error) {
  const QString proxyServer = normalizeListenAddress(listenAddress, error);
  if (proxyServer.isEmpty()) {
    return false;
  }
  const QString proxyUrl = "http://" + proxyServer;

  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  if (settings.value("active", false).toBool()) {
    const bool alreadyEnabled =
        settings.value("applied_server").toString() == proxyServer;
    settings.endGroup();
    if (alreadyEnabled) {
      return true;
    }
    if (!restoreSavedSettings(error, true)) {
      return false;
    }
    settings.beginGroup(kSettingsGroup);
  }

  HKEY key = nullptr;
  LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0,
                          KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
  if (rc != ERROR_SUCCESS) {
    settings.endGroup();
    if (error != nullptr) {
      *error = windowsError("Failed to open system proxy settings", rc);
    }
    return false;
  }

  HKEY envKey = nullptr;
  rc = RegOpenKeyExW(HKEY_CURRENT_USER, kEnvironmentKey, 0,
                     KEY_QUERY_VALUE | KEY_SET_VALUE, &envKey);
  if (rc != ERROR_SUCCESS) {
    RegCloseKey(key);
    settings.endGroup();
    if (error != nullptr) {
      *error = windowsError("Failed to open environment settings", rc);
    }
    return false;
  }

  RegistryValue originalEnable;
  RegistryValue originalServer;
  bool readOk = readValue(key, L"ProxyEnable", &originalEnable, error) &&
               readValue(key, L"ProxyServer", &originalServer, error);

  RegistryValue originalEnv[kProxyEnvNameCount];
  for (int i = 0; readOk && i < kProxyEnvNameCount; ++i) {
    readOk = readValue(envKey, kProxyEnvNames[i], &originalEnv[i], error);
  }

  if (!readOk) {
    RegCloseKey(envKey);
    RegCloseKey(key);
    settings.endGroup();
    return false;
  }

  saveValue(settings, "original_enable", originalEnable);
  saveValue(settings, "original_server", originalServer);
  for (int i = 0; i < kProxyEnvNameCount; ++i) {
    saveValue(settings, proxyEnvSettingsKey(i), originalEnv[i]);
  }
  settings.setValue("applied_server", proxyServer);
  settings.setValue("active", true);
  settings.sync();

  bool writeOk = writeValue(key, L"ProxyServer",
                            stringValue(proxyServer), error) &&
                writeValue(key, L"ProxyEnable", dwordValue(1), error);
  for (int i = 0; writeOk && i < kProxyEnvNameCount; ++i) {
    writeOk = writeValue(envKey, kProxyEnvNames[i], stringValue(proxyUrl),
                         error);
  }
  RegCloseKey(envKey);
  RegCloseKey(key);
  settings.endGroup();

  if (!writeOk) {
    restoreSavedSettings(nullptr, false);
    return false;
  }

  notifyProxyChanged();
  notifyEnvironmentChanged();
  return true;
}

bool WindowsSystemProxy::disable(QString *error) {
  return restoreSavedSettings(error, true);
}

void WindowsSystemProxy::recoverStaleSettings() {
  restoreSavedSettings(nullptr, true);
}
