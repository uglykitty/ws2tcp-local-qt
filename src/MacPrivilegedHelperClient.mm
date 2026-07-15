#include "MacPrivilegedHelperClient.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <ServiceManagement/ServiceManagement.h>
#include <xpc/xpc.h>

#include <dispatch/dispatch.h>
#include <unistd.h>

namespace {

constexpr char kMachService[] = "com.uglykitty.ws2tcp-local.proxy-helper";
constexpr char kCommandKey[] = "command";
constexpr char kRefreshCommand[] = "refresh-proxy-states";
constexpr char kSuccessKey[] = "success";
constexpr char kErrorKey[] = "error";
NSString *const kInstalledAppCodeHashKey =
    @"privileged_proxy_helper_app_code_hash";
NSString *const kInstalledHelperCodeHashKey =
    @"privileged_proxy_helper_code_hash";
NSString *const kInstalledAppPathKey = @"privileged_proxy_helper_app_path";
NSString *const kHelperRegistrationSchemaKey =
    @"privileged_proxy_helper_registration_schema";
constexpr NSInteger kHelperRegistrationSchema = 3;

QString fromNSString(NSString *value) {
  return value != nil ? QString::fromUtf8(value.UTF8String) : QString();
}

NSString *codeHashAtURL(NSURL *url) {
  SecStaticCodeRef code = nullptr;
  CFDictionaryRef information = nullptr;
  if (url == nil ||
      SecStaticCodeCreateWithPath((__bridge CFURLRef)url, kSecCSDefaultFlags,
                                  &code) != errSecSuccess ||
      SecCodeCopySigningInformation(code, kSecCSSigningInformation,
                                    &information) != errSecSuccess) {
    if (information != nullptr) CFRelease(information);
    if (code != nullptr) CFRelease(code);
    return nil;
  }
  NSData *hash = [(__bridge NSDictionary *)information
      objectForKey:(__bridge NSString *)kSecCodeInfoUnique];
  NSMutableString *hex = [NSMutableString stringWithCapacity:hash.length * 2];
  const auto *bytes = static_cast<const unsigned char *>(hash.bytes);
  for (NSUInteger i = 0; i < hash.length; ++i) {
    [hex appendFormat:@"%02x", bytes[i]];
  }
  CFRelease(information);
  CFRelease(code);
  return hex;
}

NSString *teamIdentifierAtURL(NSURL *url) {
  SecStaticCodeRef code = nullptr;
  CFDictionaryRef information = nullptr;
  if (url == nil ||
      SecStaticCodeCreateWithPath((__bridge CFURLRef)url, kSecCSDefaultFlags,
                                  &code) != errSecSuccess ||
      SecCodeCopySigningInformation(code, kSecCSSigningInformation,
                                    &information) != errSecSuccess) {
    if (information != nullptr) CFRelease(information);
    if (code != nullptr) CFRelease(code);
    return nil;
  }
  NSString *team = [(__bridge NSDictionary *)information
      objectForKey:(__bridge NSString *)kSecCodeInfoTeamIdentifier];
  team = [[team copy] autorelease];
  CFRelease(information);
  CFRelease(code);
  return team;
}

bool ensureRegistered(QString *error) {
  @autoreleasepool {
    SMAppService *service = [SMAppService daemonServiceWithPlistName:
        @"com.uglykitty.ws2tcp-local.proxy-helper.plist"];
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    NSURL *appURL = NSBundle.mainBundle.bundleURL.URLByStandardizingPath;
    NSURL *helperURL = [appURL
        URLByAppendingPathComponent:
            @"Contents/Library/HelperTools/ws2tcp-proxy-helper"];
    NSString *appTeam = teamIdentifierAtURL(appURL);
    NSString *helperTeam = teamIdentifierAtURL(helperURL);
    if (appTeam.length == 0 || helperTeam.length == 0) {
      if (error != nullptr) {
        *error = "The privileged proxy helper cannot run with an ad-hoc "
                 "signature. Install an Apple Development certificate and "
                 "reconfigure CMake with "
                 "-DWS2TCP_MACOS_SIGNING_IDENTITY=<certificate identity>";
      }
      return false;
    }
    if (![appTeam isEqualToString:helperTeam]) {
      if (error != nullptr) {
        *error = "The app and privileged proxy helper must be signed by the "
                 "same Apple Developer team";
      }
      return false;
    }
    NSString *appCodeHash = codeHashAtURL(appURL);
    NSString *helperCodeHash = codeHashAtURL(helperURL);
    NSString *appPath = appURL.path;
    NSString *installedCodeHash =
        [defaults stringForKey:kInstalledAppCodeHashKey];
    NSString *installedHelperCodeHash =
        [defaults stringForKey:kInstalledHelperCodeHashKey];
    NSString *installedAppPath = [defaults stringForKey:kInstalledAppPathKey];
    const NSInteger installedSchema =
        [defaults integerForKey:kHelperRegistrationSchemaKey];
    if (service.status == SMAppServiceStatusEnabled &&
        installedSchema == kHelperRegistrationSchema &&
        appCodeHash != nil &&
        [installedCodeHash isEqualToString:appCodeHash] &&
        helperCodeHash != nil &&
        [installedHelperCodeHash isEqualToString:helperCodeHash] &&
        appPath != nil && [installedAppPath isEqualToString:appPath]) {
      return true;
    }
    if (service.status == SMAppServiceStatusEnabled) {
      NSError *unregisterError = nil;
      if (![service unregisterAndReturnError:&unregisterError]) {
        if (error != nullptr) {
          *error = "Failed to update privileged proxy helper: " +
                   fromNSString(unregisterError.localizedDescription);
        }
        return false;
      }
      constexpr int kUnregisterPollCount = 50;
      for (int attempt = 0;
           attempt < kUnregisterPollCount &&
           service.status != SMAppServiceStatusNotRegistered;
           ++attempt) {
        usleep(100000);
      }
      if (service.status != SMAppServiceStatusNotRegistered) {
        if (error != nullptr) {
          *error = "Timed out while removing the previous privileged proxy "
                   "helper registration";
        }
        return false;
      }
    }
    if (service.status == SMAppServiceStatusRequiresApproval) {
      if (error != nullptr) {
        *error = "The privileged proxy helper requires approval in System "
                 "Settings > General > Login Items";
      }
      return false;
    }

    NSError *registrationError = nil;
    if (![service registerAndReturnError:&registrationError]) {
      if (error != nullptr) {
        *error = "Failed to register privileged proxy helper: " +
                 fromNSString(registrationError.localizedDescription);
      }
      return false;
    }
    // Registration can succeed before the administrator approves the daemon.
    // Persist the registered build now so that the next attempt does not
    // unregister the daemon that the user just approved.
    if (appCodeHash != nil)
      [defaults setObject:appCodeHash forKey:kInstalledAppCodeHashKey];
    if (helperCodeHash != nil)
      [defaults setObject:helperCodeHash forKey:kInstalledHelperCodeHashKey];
    if (appPath != nil)
      [defaults setObject:appPath forKey:kInstalledAppPathKey];
    [defaults setInteger:kHelperRegistrationSchema
                  forKey:kHelperRegistrationSchemaKey];
    if (service.status != SMAppServiceStatusEnabled) {
      if (error != nullptr) {
        *error = "The privileged proxy helper was registered but requires "
                 "approval in System Settings > General > Login Items";
      }
      return false;
    }
    return true;
  }
}

}  // namespace

bool MacPrivilegedHelperClient::refreshProxyStates(QString *error) {
  if (!ensureRegistered(error)) {
    return false;
  }

  xpc_connection_t connection = xpc_connection_create_mach_service(
      kMachService, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
      XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
  if (connection == nullptr) {
    if (error != nullptr) *error = "Failed to create proxy helper connection";
    return false;
  }
  xpc_connection_set_event_handler(connection, ^(xpc_object_t) {});
  xpc_connection_resume(connection);

  xpc_object_t request = xpc_dictionary_create(nullptr, nullptr, 0);
  xpc_dictionary_set_string(request, kCommandKey, kRefreshCommand);
  dispatch_semaphore_t completed = dispatch_semaphore_create(0);
  __block xpc_object_t reply = nullptr;
  xpc_connection_send_message_with_reply(
      connection, request,
      dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
      ^(xpc_object_t response) {
        if (response != nullptr) reply = xpc_retain(response);
        dispatch_semaphore_signal(completed);
      });
  xpc_release(request);

  // The helper invokes networksetup four times for every active network
  // service. Five seconds is too short on systems with several services.
  constexpr int64_t kHelperTimeoutNanoseconds = 30 * NSEC_PER_SEC;
  const bool timedOut =
      dispatch_semaphore_wait(
          completed,
          dispatch_time(DISPATCH_TIME_NOW, kHelperTimeoutNanoseconds)) != 0;
  xpc_connection_cancel(connection);
  xpc_release(connection);

  if (timedOut) {
    if (error != nullptr) {
      SMAppService *service = [SMAppService daemonServiceWithPlistName:
          @"com.uglykitty.ws2tcp-local.proxy-helper.plist"];
      *error = service.status == SMAppServiceStatusRequiresApproval
                   ? "The privileged proxy helper requires approval in "
                     "System Settings > General > Login Items"
                   : "The privileged proxy helper is enabled but did not "
                     "respond. Reinstall or rebuild the app to refresh its "
                     "helper registration, then try again";
    }
    return false;
  }

  if (reply == nullptr || xpc_get_type(reply) == XPC_TYPE_ERROR) {
    if (error != nullptr) {
      const char *description = reply != nullptr
                                    ? xpc_dictionary_get_string(
                                          reply, XPC_ERROR_KEY_DESCRIPTION)
                                    : nullptr;
      *error = description != nullptr
                   ? "Privileged proxy helper failed: " +
                         QString::fromUtf8(description)
                   : "Privileged proxy helper did not reply";
    }
    if (reply != nullptr) xpc_release(reply);
    return false;
  }

  const bool success = xpc_dictionary_get_bool(reply, kSuccessKey);
  if (!success && error != nullptr) {
    const char *message = xpc_dictionary_get_string(reply, kErrorKey);
    *error = message != nullptr ? QString::fromUtf8(message)
                                : "Privileged proxy helper rejected request";
  }
  xpc_release(reply);
  return success;
}
