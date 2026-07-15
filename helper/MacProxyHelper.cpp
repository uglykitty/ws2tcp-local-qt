#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <xpc/xpc.h>

#include <dispatch/dispatch.h>

#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

constexpr char kCommandKey[] = "command";
constexpr char kRefreshCommand[] = "refresh-proxy-states";
constexpr char kSuccessKey[] = "success";
constexpr char kErrorKey[] = "error";
constexpr char kMachService[] = "com.uglykitty.ws2tcp-local.proxy-helper";

template <typename T>
class CfRef {
 public:
  explicit CfRef(T value = nullptr) : value_(value) {}
  ~CfRef() {
    if (value_) CFRelease(value_);
  }
  CfRef(const CfRef &) = delete;
  CfRef &operator=(const CfRef &) = delete;
  T get() const { return value_; }
  explicit operator bool() const { return value_ != nullptr; }

 private:
  T value_;
};

std::string stringValue(CFStringRef value) {
  if (!value) return {};
  const CFIndex size = CFStringGetMaximumSizeForEncoding(
                           CFStringGetLength(value), kCFStringEncodingUTF8) +
                       1;
  std::vector<char> buffer(static_cast<size_t>(size));
  return CFStringGetCString(value, buffer.data(), size, kCFStringEncodingUTF8)
             ? std::string(buffer.data())
             : std::string();
}

bool runNetworkSetup(const char *operation, const std::string &service,
                     const char *state) {
  const pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    execl("/usr/sbin/networksetup", "networksetup", operation,
          service.c_str(), state, static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = 0;
  return waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0;
}

bool refreshProxyStates(std::string *error) {
  CfRef<SCPreferencesRef> preferences(SCPreferencesCreate(
      kCFAllocatorDefault, CFSTR("ws2tcp-proxy-helper"), nullptr));
  CfRef<SCNetworkSetRef> set(
      preferences ? SCNetworkSetCopyCurrent(preferences.get()) : nullptr);
  CfRef<CFArrayRef> services(set ? SCNetworkSetCopyServices(set.get()) : nullptr);
  if (!preferences || !set || !services) {
    *error = "Failed to enumerate macOS network services";
    return false;
  }

  bool found = false;
  for (CFIndex i = 0; i < CFArrayGetCount(services.get()); ++i) {
    auto service = static_cast<SCNetworkServiceRef>(
        const_cast<void *>(CFArrayGetValueAtIndex(services.get(), i)));
    if (!SCNetworkServiceGetEnabled(service)) continue;
    CfRef<SCNetworkProtocolRef> protocol(
        SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeProxies));
    const std::string name = stringValue(SCNetworkServiceGetName(service));
    if (!protocol || name.empty()) continue;
    found = true;
    if (!runNetworkSetup("-setwebproxystate", name, "off") ||
        !runNetworkSetup("-setsecurewebproxystate", name, "off") ||
        !runNetworkSetup("-setwebproxystate", name, "on") ||
        !runNetworkSetup("-setsecurewebproxystate", name, "on")) {
      *error = "networksetup failed for service: " + name;
      return false;
    }
  }
  if (!found) *error = "No enabled network service supports proxy settings";
  return found;
}

void handleMessage(xpc_object_t message) {
  xpc_object_t reply = xpc_dictionary_create_reply(message);
  if (!reply) return;
  const char *command = xpc_dictionary_get_string(message, kCommandKey);
  std::string error;
  const bool success =
      command != nullptr && std::string(command) == kRefreshCommand &&
      refreshProxyStates(&error);
  xpc_dictionary_set_bool(reply, kSuccessKey, success);
  if (!success) {
    if (error.empty()) error = "Unsupported privileged helper request";
    xpc_dictionary_set_string(reply, kErrorKey, error.c_str());
  }
  xpc_connection_t remote = xpc_dictionary_get_remote_connection(message);
  xpc_connection_send_message(remote, reply);
  xpc_release(reply);
}

void handleConnection(xpc_connection_t connection) {
  xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
    if (xpc_get_type(event) == XPC_TYPE_DICTIONARY) handleMessage(event);
  });
  xpc_connection_resume(connection);
}

}  // namespace

int main() {
  if (geteuid() != 0) return 1;
  xpc_connection_t listener = xpc_connection_create_mach_service(
      kMachService, dispatch_get_main_queue(),
      XPC_CONNECTION_MACH_SERVICE_LISTENER);
  if (listener == nullptr) return 1;
  xpc_connection_set_event_handler(listener, ^(xpc_object_t event) {
    if (xpc_get_type(event) == XPC_TYPE_CONNECTION)
      handleConnection(static_cast<xpc_connection_t>(event));
  });
  xpc_connection_resume(listener);
  dispatch_main();
}
