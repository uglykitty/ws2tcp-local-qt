#include "MacSystemProxy.h"

#include <QByteArray>
#include <QSettings>
#include <QVariantMap>

#include <SystemConfiguration/SystemConfiguration.h>
#include <Security/Authorization.h>

namespace {

constexpr char kSettingsGroup[] = "system_proxy/macos";

class Authorization final {
 public:
  Authorization() {
    status_ = AuthorizationCreate(nullptr, nullptr, kAuthorizationFlagDefaults,
                                  &reference_);
  }
  ~Authorization() {
    if (reference_ != nullptr) {
      AuthorizationFree(reference_, kAuthorizationFlagDefaults);
    }
  }

  bool isValid() const { return status_ == errAuthorizationSuccess; }
  AuthorizationRef get() const { return reference_; }
  OSStatus status() const { return status_; }

 private:
  AuthorizationRef reference_ = nullptr;
  OSStatus status_ = errAuthorizationSuccess;
};

template <typename T>
class CfRef final {
 public:
  explicit CfRef(T value = nullptr) : value_(value) {}
  ~CfRef() {
    if (value_ != nullptr) {
      CFRelease(value_);
    }
  }
  CfRef(const CfRef &) = delete;
  CfRef &operator=(const CfRef &) = delete;
  CfRef(CfRef &&other) noexcept : value_(other.value_) {
    other.value_ = nullptr;
  }
  CfRef &operator=(CfRef &&other) noexcept {
    if (this != &other) {
      if (value_ != nullptr) {
        CFRelease(value_);
      }
      value_ = other.value_;
      other.value_ = nullptr;
    }
    return *this;
  }
  T get() const { return value_; }
  explicit operator bool() const { return value_ != nullptr; }

 private:
  T value_;
};

QString systemConfigurationError(const QString &operation) {
  const int code = SCError();
  const char *description = SCErrorString(code);
  return QString("%1: %2 (SystemConfiguration error %3)")
      .arg(operation,
           description != nullptr ? QString::fromUtf8(description)
                                  : QString("unknown error"))
      .arg(code);
}

QString serviceId(SCNetworkServiceRef service) {
  CFStringRef identifier = SCNetworkServiceGetServiceID(service);
  if (identifier == nullptr) {
    return {};
  }
  const CFIndex length = CFStringGetLength(identifier);
  const CFIndex maximum =
      CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  QByteArray utf8(maximum, Qt::Uninitialized);
  if (!CFStringGetCString(identifier, utf8.data(), maximum,
                          kCFStringEncodingUTF8)) {
    return {};
  }
  return QString::fromUtf8(utf8.constData());
}

QByteArray serializeDictionary(CFDictionaryRef dictionary) {
  if (dictionary == nullptr) {
    return {};
  }
  CFErrorRef rawError = nullptr;
  CfRef<CFDataRef> data(CFPropertyListCreateData(
      kCFAllocatorDefault, dictionary, kCFPropertyListBinaryFormat_v1_0, 0,
      &rawError));
  CfRef<CFErrorRef> error(rawError);
  if (!data) {
    return {};
  }
  return QByteArray(
      reinterpret_cast<const char *>(CFDataGetBytePtr(data.get())),
      CFDataGetLength(data.get()));
}

CfRef<CFDictionaryRef> deserializeDictionary(const QByteArray &data) {
  if (data.isEmpty()) {
    return CfRef<CFDictionaryRef>();
  }
  CfRef<CFDataRef> cfData(CFDataCreate(
      kCFAllocatorDefault,
      reinterpret_cast<const UInt8 *>(data.constData()), data.size()));
  CFErrorRef rawError = nullptr;
  CFPropertyListRef propertyList = CFPropertyListCreateWithData(
      kCFAllocatorDefault, cfData.get(), kCFPropertyListImmutable, nullptr,
      &rawError);
  CfRef<CFErrorRef> error(rawError);
  if (propertyList == nullptr || CFGetTypeID(propertyList) !=
                                     CFDictionaryGetTypeID()) {
    if (propertyList != nullptr) {
      CFRelease(propertyList);
    }
    return CfRef<CFDictionaryRef>();
  }
  return CfRef<CFDictionaryRef>(
      static_cast<CFDictionaryRef>(propertyList));
}

bool parseListenAddress(const QString &listenAddress, QString *host, int *port,
                        QString *error) {
  const QString input = listenAddress.trimmed();
  const int separator = input.lastIndexOf(':');
  bool portOk = false;
  const int parsedPort = input.mid(separator + 1).toInt(&portOk);
  if (separator <= 0 || !portOk || parsedPort < 1 || parsedPort > 65535) {
    if (error != nullptr) {
      *error = "Listen address must contain a valid host and port";
    }
    return false;
  }

  QString parsedHost = input.left(separator).trimmed();
  if (parsedHost.startsWith('[') && parsedHost.endsWith(']')) {
    parsedHost = parsedHost.mid(1, parsedHost.size() - 2);
  }
  if (parsedHost == "0.0.0.0" || parsedHost == "::" || parsedHost == "*") {
    parsedHost = "127.0.0.1";
  }
  if (parsedHost.isEmpty()) {
    if (error != nullptr) {
      *error = "Listen address must contain a host";
    }
    return false;
  }
  *host = parsedHost;
  *port = parsedPort;
  return true;
}

CfRef<CFMutableDictionaryRef> proxyConfiguration(CFDictionaryRef original,
                                                 const QString &host,
                                                 int port) {
  CfRef<CFMutableDictionaryRef> configuration(
      original != nullptr
          ? CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, original)
          : CFDictionaryCreateMutable(
                kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks));
  if (!configuration) {
    return configuration;
  }

  CfRef<CFStringRef> cfHost(CFStringCreateWithCString(
      kCFAllocatorDefault, host.toUtf8().constData(), kCFStringEncodingUTF8));
  const int number = port;
  CfRef<CFNumberRef> cfPort(CFNumberCreate(kCFAllocatorDefault,
                                           kCFNumberIntType, &number));
  if (!cfHost || !cfPort) {
    return CfRef<CFMutableDictionaryRef>();
  }
  CFDictionarySetValue(configuration.get(), kSCPropNetProxiesHTTPEnable,
                       kCFBooleanTrue);
  CFDictionarySetValue(configuration.get(), kSCPropNetProxiesHTTPProxy,
                       cfHost.get());
  CFDictionarySetValue(configuration.get(), kSCPropNetProxiesHTTPPort,
                       cfPort.get());
  CFDictionarySetValue(configuration.get(), kSCPropNetProxiesHTTPSEnable,
                       kCFBooleanTrue);
  CFDictionarySetValue(configuration.get(), kSCPropNetProxiesHTTPSProxy,
                       cfHost.get());
  CFDictionarySetValue(configuration.get(), kSCPropNetProxiesHTTPSPort,
                       cfPort.get());

  const void *bypassValues[] = {CFSTR("localhost"), CFSTR("127.0.0.1"),
                                CFSTR("::1"), CFSTR("*.local"),
                                CFSTR("169.254/16")};
  CfRef<CFMutableArrayRef> exceptions;
  auto existing = static_cast<CFArrayRef>(CFDictionaryGetValue(
      configuration.get(), kSCPropNetProxiesExceptionsList));
  if (existing != nullptr && CFGetTypeID(existing) == CFArrayGetTypeID()) {
    exceptions = CfRef<CFMutableArrayRef>(
        CFArrayCreateMutableCopy(kCFAllocatorDefault, 0, existing));
  } else {
    exceptions = CfRef<CFMutableArrayRef>(CFArrayCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
  }
  if (!exceptions) {
    return CfRef<CFMutableDictionaryRef>();
  }
  for (const void *value : bypassValues) {
    if (!CFArrayContainsValue(exceptions.get(),
                              CFRangeMake(0, CFArrayGetCount(exceptions.get())),
                              value)) {
      CFArrayAppendValue(exceptions.get(), value);
    }
  }
  CFDictionarySetValue(configuration.get(), kSCPropNetProxiesExceptionsList,
                       exceptions.get());
  return configuration;
}

bool commitChanges(SCPreferencesRef preferences, QString *error) {
  if (!SCPreferencesCommitChanges(preferences)) {
    if (error != nullptr) {
      *error = systemConfigurationError("Failed to save system proxy settings");
    }
    return false;
  }
  if (!SCPreferencesApplyChanges(preferences)) {
    if (error != nullptr) {
      *error = systemConfigurationError("Failed to apply system proxy settings");
    }
    return false;
  }
  return true;
}

CfRef<SCPreferencesRef> createPreferences(AuthorizationRef authorization,
                                          QString *error) {
  CfRef<SCPreferencesRef> preferences(SCPreferencesCreateWithAuthorization(
      kCFAllocatorDefault, CFSTR("ws2tcp-local"), nullptr, authorization));
  if (!preferences && error != nullptr) {
    *error = systemConfigurationError(
        "Failed to open macOS network preferences");
  }
  return preferences;
}

bool restoreSavedSettings(QString *error, bool onlyIfOwned) {
  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  if (!settings.value("active", false).toBool()) {
    settings.endGroup();
    return true;
  }
  const QVariantMap originals = settings.value("originals").toMap();
  const QVariantMap applied = settings.value("applied").toMap();

  Authorization authorization;
  if (!authorization.isValid()) {
    settings.endGroup();
    if (error != nullptr) {
      *error = QString("Failed to create macOS authorization (error %1)")
                   .arg(authorization.status());
    }
    return false;
  }
  auto preferences = createPreferences(authorization.get(), error);
  if (!preferences) {
    settings.endGroup();
    return false;
  }
  CfRef<SCNetworkSetRef> networkSet(
      SCNetworkSetCopyCurrent(preferences.get()));
  CfRef<CFArrayRef> services(networkSet
                                 ? SCNetworkSetCopyServices(networkSet.get())
                                 : nullptr);
  if (!networkSet || !services) {
    settings.endGroup();
    if (error != nullptr) {
      *error = systemConfigurationError("Failed to enumerate network services");
    }
    return false;
  }

  bool changed = false;
  for (CFIndex index = 0; index < CFArrayGetCount(services.get()); ++index) {
    auto service = static_cast<SCNetworkServiceRef>(
        const_cast<void *>(CFArrayGetValueAtIndex(services.get(), index)));
    const QString id = serviceId(service);
    if (!originals.contains(id)) {
      continue;
    }
    CfRef<SCNetworkProtocolRef> protocol(
        SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeProxies));
    if (!protocol) {
      continue;
    }
    CFDictionaryRef current = SCNetworkProtocolGetConfiguration(protocol.get());
    auto expected = deserializeDictionary(applied.value(id).toByteArray());
    const bool stillOwned = expected && current != nullptr &&
                            CFEqual(current, expected.get());
    if (onlyIfOwned && !stillOwned) {
      continue;
    }

    const QByteArray originalData = originals.value(id).toByteArray();
    bool ok = false;
    if (originalData.isEmpty()) {
      ok = SCNetworkProtocolSetConfiguration(protocol.get(), nullptr);
    } else {
      auto original = deserializeDictionary(originalData);
      ok = original &&
           SCNetworkProtocolSetConfiguration(protocol.get(), original.get());
    }
    if (!ok) {
      settings.endGroup();
      if (error != nullptr) {
        *error = systemConfigurationError(
            "Failed to restore system proxy settings");
      }
      return false;
    }
    changed = true;
  }

  if (changed && !commitChanges(preferences.get(), error)) {
    settings.endGroup();
    return false;
  }
  settings.remove("");
  settings.endGroup();
  return true;
}

}  // namespace

bool MacSystemProxy::enable(const QString &listenAddress, QString *error) {
  QString host;
  int port = 0;
  if (!parseListenAddress(listenAddress, &host, &port, error)) {
    return false;
  }

  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  if (settings.value("active", false).toBool()) {
    const bool alreadyEnabled = settings.value("host").toString() == host &&
                                settings.value("port").toInt() == port;
    settings.endGroup();
    if (alreadyEnabled) {
      return true;
    }
    if (!restoreSavedSettings(error, true)) {
      return false;
    }
    settings.beginGroup(kSettingsGroup);
  }

  Authorization authorization;
  if (!authorization.isValid()) {
    settings.endGroup();
    if (error != nullptr) {
      *error = QString("Failed to create macOS authorization (error %1)")
                   .arg(authorization.status());
    }
    return false;
  }
  auto preferences = createPreferences(authorization.get(), error);
  if (!preferences) {
    settings.endGroup();
    return false;
  }
  CfRef<SCNetworkSetRef> networkSet(
      SCNetworkSetCopyCurrent(preferences.get()));
  CfRef<CFArrayRef> services(networkSet
                                 ? SCNetworkSetCopyServices(networkSet.get())
                                 : nullptr);
  if (!networkSet || !services) {
    settings.endGroup();
    if (error != nullptr) {
      *error = systemConfigurationError("Failed to enumerate network services");
    }
    return false;
  }

  QVariantMap originals;
  QVariantMap applied;
  for (CFIndex index = 0; index < CFArrayGetCount(services.get()); ++index) {
    auto service = static_cast<SCNetworkServiceRef>(
        const_cast<void *>(CFArrayGetValueAtIndex(services.get(), index)));
    if (!SCNetworkServiceGetEnabled(service)) {
      continue;
    }
    CfRef<SCNetworkProtocolRef> protocol(
        SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeProxies));
    if (!protocol) {
      continue;
    }
    const QString id = serviceId(service);
    if (id.isEmpty()) {
      continue;
    }
    CFDictionaryRef original = SCNetworkProtocolGetConfiguration(protocol.get());
    auto configuration = proxyConfiguration(original, host, port);
    if (!configuration) {
      settings.endGroup();
      if (error != nullptr) {
        *error = "Failed to allocate system proxy configuration";
      }
      return false;
    }
    const QByteArray originalData = serializeDictionary(original);
    const QByteArray appliedData = serializeDictionary(configuration.get());
    if ((original != nullptr && originalData.isEmpty()) ||
        appliedData.isEmpty()) {
      settings.endGroup();
      if (error != nullptr) {
        *error = "Failed to save system proxy recovery data";
      }
      return false;
    }
    originals.insert(id, originalData);
    applied.insert(id, appliedData);
    if (!SCNetworkProtocolSetConfiguration(protocol.get(), configuration.get())) {
      settings.endGroup();
      if (error != nullptr) {
        *error = systemConfigurationError("Failed to set system proxy settings");
      }
      return false;
    }
  }
  if (originals.isEmpty()) {
    settings.endGroup();
    if (error != nullptr) {
      *error = "No enabled macOS network services support proxy settings";
    }
    return false;
  }

  settings.setValue("originals", originals);
  settings.setValue("applied", applied);
  settings.setValue("host", host);
  settings.setValue("port", port);
  settings.setValue("active", true);
  settings.sync();

  if (!commitChanges(preferences.get(), error)) {
    settings.endGroup();
    restoreSavedSettings(nullptr, false);
    return false;
  }
  settings.endGroup();
  return true;
}

bool MacSystemProxy::disable(QString *error) {
  return restoreSavedSettings(error, true);
}

void MacSystemProxy::recoverStaleSettings() {
  restoreSavedSettings(nullptr, true);
}
