#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <dirent.h>
#include <limits.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static const char *kProgram = "auto-low-power-listener";
static const char *kPmsetPath = "/usr/bin/pmset";
static const char *kPowerPreferencesDir = "/Library/Preferences";
static const char *kPowerManagementPrefix = "com.apple.PowerManagement";
static const char *kPlistSuffix = ".plist";

static IONotificationPortRef gNotificationPort = NULL;
static io_iterator_t gBatteryIterator = IO_OBJECT_NULL;
static io_iterator_t gManagerIterator = IO_OBJECT_NULL;
static io_object_t gBatteryInterest = IO_OBJECT_NULL;
static io_object_t gManagerInterest = IO_OBJECT_NULL;
static io_service_t gBatteryService = IO_OBJECT_NULL;
static io_service_t gManagerService = IO_OBJECT_NULL;
static char gPowerPlistPath[PATH_MAX] = {0};
static int gThreshold = 25;
static bool gDebug = false;
static double gApplyCooldownSeconds = 3.0;
static bool gHasRecentApply = false;
static int gRecentDesiredBatteryMode = -1;
static int gRecentDesiredACMode = -1;
static CFAbsoluteTime gRecentApplyAt = 0;

static void log_message(const char *level, const char *message) {
  fprintf(stderr, "[%s] %s: %s\n", kProgram, level, message);
  fflush(stderr);
}

static void log_debug(const char *message) {
  if (!gDebug) {
    return;
  }

  log_message("debug", message);
}

static void log_event_snapshot(
  const char *reason,
  int batteryPercent,
  bool externalConnected,
  int actualBatteryMode,
  int actualACMode
) {
  if (!gDebug) {
    return;
  }

  fprintf(
    stderr,
    "[%s] debug: event=%s source=%s battery=%d%% actual_b=%d actual_c=%d\n",
    kProgram,
    reason,
    externalConnected ? "AC" : "Battery",
    batteryPercent,
    actualBatteryMode,
    actualACMode
  );
  fflush(stderr);
}

static void log_correction(
  const char *reason,
  int batteryPercent,
  bool externalConnected,
  int desiredBatteryMode,
  int actualBatteryMode,
  int actualACMode
) {
  fprintf(
    stderr,
    "[%s] info: corrected after %s source=%s battery=%d%% battery_mode %d->%d ac_mode %d->0\n",
    kProgram,
    reason,
    externalConnected ? "AC" : "Battery",
    batteryPercent,
    actualBatteryMode,
    desiredBatteryMode,
    actualACMode
  );
  fflush(stderr);
}

static void log_duplicate_skip(
  const char *reason,
  int batteryPercent,
  bool externalConnected,
  int desiredBatteryMode
) {
  if (!gDebug) {
    return;
  }

  fprintf(
    stderr,
    "[%s] debug: skipped duplicate correction after %s source=%s battery=%d%% desired_b=%d\n",
    kProgram,
    reason,
    externalConnected ? "AC" : "Battery",
    batteryPercent,
    desiredBatteryMode
  );
  fflush(stderr);
}

static const char *message_name(natural_t messageType) {
  switch (messageType) {
    case kIOMessageServicePropertyChange:
      return "property-change";
    case kIOMessageServiceBusyStateChange:
      return "busy-state-change";
    case kIOMessageDeviceWillPowerOff:
      return "device-will-power-off";
    case kIOMessageDeviceHasPoweredOn:
      return "device-has-powered-on";
    case kIOMessageServiceIsSuspended:
      return "service-suspended";
    case kIOMessageServiceIsResumed:
      return "service-resumed";
    case kIOMessageServiceIsTerminated:
      return "service-terminated";
    default:
      return "interest-message";
  }
}

static bool string_has_prefix(const char *value, const char *prefix) {
  return strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool string_has_suffix(const char *value, const char *suffix) {
  size_t valueLength = strlen(value);
  size_t suffixLength = strlen(suffix);
  if (suffixLength > valueLength) {
    return false;
  }

  return strcmp(value + valueLength - suffixLength, suffix) == 0;
}

static CFPropertyListRef load_plist(const char *path) {
  CFPropertyListRef plist = NULL;
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
    kCFAllocatorDefault,
    (const UInt8 *)path,
    (CFIndex)strlen(path),
    false
  );

  if (url == NULL) {
    return NULL;
  }

  CFReadStreamRef stream = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
  CFRelease(url);
  if (stream == NULL) {
    return NULL;
  }

  if (!CFReadStreamOpen(stream)) {
    CFRelease(stream);
    return NULL;
  }

  CFErrorRef error = NULL;
  plist = CFPropertyListCreateWithStream(
    kCFAllocatorDefault,
    stream,
    0,
    kCFPropertyListImmutable,
    NULL,
    &error
  );

  if (error != NULL) {
    CFRelease(error);
  }

  CFReadStreamClose(stream);
  CFRelease(stream);
  return plist;
}

static bool read_dictionary_int(CFDictionaryRef dictionary, CFStringRef key, int *value) {
  const void *rawValue = CFDictionaryGetValue(dictionary, key);
  if (rawValue == NULL || CFGetTypeID(rawValue) != CFNumberGetTypeID()) {
    return false;
  }

  return CFNumberGetValue((CFNumberRef)rawValue, kCFNumberIntType, value);
}

static bool read_profile_mode(CFDictionaryRef root, CFStringRef profileKey, int *mode) {
  const void *profileValue = CFDictionaryGetValue(root, profileKey);
  if (profileValue == NULL || CFGetTypeID(profileValue) != CFDictionaryGetTypeID()) {
    return false;
  }

  return read_dictionary_int((CFDictionaryRef)profileValue, CFSTR("LowPowerMode"), mode);
}

static bool power_plist_has_modes(const char *path) {
  bool ok = false;
  CFPropertyListRef plist = load_plist(path);
  if (plist == NULL || CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
    if (plist != NULL) {
      CFRelease(plist);
    }
    return false;
  }

  int batteryMode = 0;
  int acMode = 0;
  ok = read_profile_mode((CFDictionaryRef)plist, CFSTR("Battery Power"), &batteryMode) &&
       read_profile_mode((CFDictionaryRef)plist, CFSTR("AC Power"), &acMode);

  CFRelease(plist);
  return ok;
}

static bool find_power_plist(void) {
  if (gPowerPlistPath[0] != '\0' && access(gPowerPlistPath, R_OK) == 0) {
    return true;
  }

  DIR *directory = opendir(kPowerPreferencesDir);
  if (directory == NULL) {
    return false;
  }

  struct dirent *entry = NULL;
  while ((entry = readdir(directory)) != NULL) {
    if (!string_has_prefix(entry->d_name, kPowerManagementPrefix) ||
        !string_has_suffix(entry->d_name, kPlistSuffix)) {
      continue;
    }

    char candidate[PATH_MAX];
    int written = snprintf(candidate, sizeof(candidate), "%s/%s", kPowerPreferencesDir, entry->d_name);
    if (written <= 0 || written >= (int)sizeof(candidate)) {
      continue;
    }

    if (!power_plist_has_modes(candidate)) {
      continue;
    }

    strncpy(gPowerPlistPath, candidate, sizeof(gPowerPlistPath) - 1);
    gPowerPlistPath[sizeof(gPowerPlistPath) - 1] = '\0';
    closedir(directory);
    return true;
  }

  closedir(directory);
  return false;
}

static bool read_actual_modes(int *batteryMode, int *acMode) {
  if (!find_power_plist()) {
    return false;
  }

  CFPropertyListRef plist = load_plist(gPowerPlistPath);
  if (plist == NULL || CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
    gPowerPlistPath[0] = '\0';
    if (plist != NULL) {
      CFRelease(plist);
    }
    return false;
  }

  bool ok = read_profile_mode((CFDictionaryRef)plist, CFSTR("Battery Power"), batteryMode) &&
            read_profile_mode((CFDictionaryRef)plist, CFSTR("AC Power"), acMode);

  CFRelease(plist);

  if (!ok) {
    gPowerPlistPath[0] = '\0';
  }

  return ok;
}

static bool fetch_power_snapshot(bool *externalConnected, int *batteryPercent) {
  bool foundBattery = false;
  bool onACPower = false;
  int computedPercent = 0;

  CFTypeRef snapshot = IOPSCopyPowerSourcesInfo();
  if (snapshot == NULL) {
    return false;
  }

  CFStringRef sourceType = IOPSGetProvidingPowerSourceType(snapshot);
  if (sourceType != NULL &&
      CFStringCompare(sourceType, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo) {
    onACPower = true;
  }

  CFArrayRef sources = IOPSCopyPowerSourcesList(snapshot);
  if (sources != NULL) {
    CFIndex count = CFArrayGetCount(sources);
    for (CFIndex i = 0; i < count; i++) {
      CFTypeRef source = CFArrayGetValueAtIndex(sources, i);
      CFDictionaryRef description = IOPSGetPowerSourceDescription(snapshot, source);
      if (description == NULL) {
        continue;
      }

      CFStringRef type = CFDictionaryGetValue(description, CFSTR(kIOPSTypeKey));
      if (type == NULL || CFGetTypeID(type) != CFStringGetTypeID()) {
        continue;
      }

      if (CFStringCompare(type, CFSTR(kIOPSInternalBatteryType), 0) != kCFCompareEqualTo) {
        continue;
      }

      int currentCapacity = 0;
      int maxCapacity = 0;
      if (!read_dictionary_int(description, CFSTR(kIOPSCurrentCapacityKey), &currentCapacity)) {
        continue;
      }

      if (read_dictionary_int(description, CFSTR(kIOPSMaxCapacityKey), &maxCapacity) &&
          maxCapacity > 0 &&
          maxCapacity != 100) {
        computedPercent = (currentCapacity * 100) / maxCapacity;
      } else {
        computedPercent = currentCapacity;
      }

      foundBattery = true;
      break;
    }

    CFRelease(sources);
  }

  CFRelease(snapshot);

  if (!foundBattery) {
    return false;
  }

  *externalConnected = onACPower;
  *batteryPercent = computedPercent;
  return true;
}

static bool run_pmset(const char *scope, int value) {
  char valueString[2];
  snprintf(valueString, sizeof(valueString), "%d", value);

  char *const argv[] = {
    (char *)kPmsetPath,
    (char *)scope,
    "lowpowermode",
    valueString,
    NULL
  };

  pid_t pid = 0;
  int spawnStatus = posix_spawn(&pid, kPmsetPath, NULL, NULL, argv, environ);
  if (spawnStatus != 0) {
    return false;
  }

  int waitStatus = 0;
  if (waitpid(pid, &waitStatus, 0) == -1) {
    return false;
  }

  return WIFEXITED(waitStatus) && WEXITSTATUS(waitStatus) == 0;
}

static bool should_skip_duplicate_apply(const char *reason, bool externalConnected, int batteryPercent, int desiredBatteryMode, int desiredACMode) {
  if (!gHasRecentApply) {
    return false;
  }

  if (desiredBatteryMode != gRecentDesiredBatteryMode || desiredACMode != gRecentDesiredACMode) {
    return false;
  }

  if ((CFAbsoluteTimeGetCurrent() - gRecentApplyAt) >= gApplyCooldownSeconds) {
    return false;
  }

  log_duplicate_skip(reason, batteryPercent, externalConnected, desiredBatteryMode);
  return true;
}

static void compute_desired_modes(bool externalConnected, int batteryPercent, int *desiredBatteryMode, int *desiredACMode) {
  *desiredACMode = 0;
  *desiredBatteryMode = (externalConnected || batteryPercent > gThreshold) ? 0 : 1;
}

static void reconcile(const char *reason) {
  bool externalConnected = false;
  int batteryPercent = 0;
  if (!fetch_power_snapshot(&externalConnected, &batteryPercent)) {
    log_message("error", "failed to read power source snapshot");
    return;
  }

  int desiredBatteryMode = 0;
  int desiredACMode = 0;
  compute_desired_modes(externalConnected, batteryPercent, &desiredBatteryMode, &desiredACMode);

  int actualBatteryMode = -1;
  int actualACMode = -1;
  if (!read_actual_modes(&actualBatteryMode, &actualACMode)) {
    log_message("error", "failed to read current low power mode settings");
    return;
  }

  log_event_snapshot(reason, batteryPercent, externalConnected, actualBatteryMode, actualACMode);

  bool needsACChange = (actualACMode != desiredACMode);
  bool needsBatteryChange = (actualBatteryMode != desiredBatteryMode);
  if (!needsACChange && !needsBatteryChange) {
    return;
  }

  if (should_skip_duplicate_apply(reason, externalConnected, batteryPercent, desiredBatteryMode, desiredACMode)) {
    return;
  }

  bool ok = true;
  if (needsACChange) {
    ok = run_pmset("-c", desiredACMode) && ok;
  }

  if (needsBatteryChange) {
    ok = run_pmset("-b", desiredBatteryMode) && ok;
  }

  if (ok) {
    gHasRecentApply = true;
    gRecentDesiredBatteryMode = desiredBatteryMode;
    gRecentDesiredACMode = desiredACMode;
    gRecentApplyAt = CFAbsoluteTimeGetCurrent();
    log_correction(reason, batteryPercent, externalConnected, desiredBatteryMode, actualBatteryMode, actualACMode);
  } else {
    log_message("error", "failed to apply desired low power mode state");
  }
}

static void battery_interest_callback(void *refCon, io_service_t service, natural_t messageType, void *messageArgument) {
  (void)refCon;
  (void)service;
  (void)messageArgument;

  if (gDebug) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "battery %s (0x%08x)", message_name(messageType), (unsigned int)messageType);
    log_debug(buffer);
  }

  if (messageType == kIOMessageServiceIsTerminated) {
    if (gBatteryInterest != IO_OBJECT_NULL) {
      IOObjectRelease(gBatteryInterest);
      gBatteryInterest = IO_OBJECT_NULL;
    }
    if (gBatteryService != IO_OBJECT_NULL) {
      IOObjectRelease(gBatteryService);
      gBatteryService = IO_OBJECT_NULL;
    }
    log_message("info", "battery service terminated");
    return;
  }

  reconcile(message_name(messageType));
}

static void manager_interest_callback(void *refCon, io_service_t service, natural_t messageType, void *messageArgument) {
  (void)refCon;
  (void)service;
  (void)messageArgument;

  if (gDebug) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "manager %s (0x%08x)", message_name(messageType), (unsigned int)messageType);
    log_debug(buffer);
  }

  if (messageType == kIOMessageServiceIsTerminated) {
    if (gManagerInterest != IO_OBJECT_NULL) {
      IOObjectRelease(gManagerInterest);
      gManagerInterest = IO_OBJECT_NULL;
    }
    if (gManagerService != IO_OBJECT_NULL) {
      IOObjectRelease(gManagerService);
      gManagerService = IO_OBJECT_NULL;
    }
    log_message("info", "battery manager service terminated");
    return;
  }

  reconcile(message_name(messageType));
}

static void battery_matched_callback(void *refCon, io_iterator_t iterator) {
  (void)refCon;

  io_service_t service = IO_OBJECT_NULL;
  while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
    if (gBatteryInterest != IO_OBJECT_NULL) {
      IOObjectRelease(gBatteryInterest);
      gBatteryInterest = IO_OBJECT_NULL;
    }

    if (gBatteryService != IO_OBJECT_NULL) {
      IOObjectRelease(gBatteryService);
      gBatteryService = IO_OBJECT_NULL;
    }

    kern_return_t status = IOServiceAddInterestNotification(
      gNotificationPort,
      service,
      kIOGeneralInterest,
      battery_interest_callback,
      NULL,
      &gBatteryInterest
    );

    if (status != KERN_SUCCESS) {
      log_message("error", "failed to register battery interest notification");
      IOObjectRelease(service);
      continue;
    }

    gBatteryService = service;
    log_message("info", "battery service matched");
    reconcile("battery-matched");
  }
}

static void manager_matched_callback(void *refCon, io_iterator_t iterator) {
  (void)refCon;

  io_service_t service = IO_OBJECT_NULL;
  while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
    if (gManagerInterest != IO_OBJECT_NULL) {
      IOObjectRelease(gManagerInterest);
      gManagerInterest = IO_OBJECT_NULL;
    }

    if (gManagerService != IO_OBJECT_NULL) {
      IOObjectRelease(gManagerService);
      gManagerService = IO_OBJECT_NULL;
    }

    kern_return_t status = IOServiceAddInterestNotification(
      gNotificationPort,
      service,
      kIOGeneralInterest,
      manager_interest_callback,
      NULL,
      &gManagerInterest
    );

    if (status != KERN_SUCCESS) {
      log_message("error", "failed to register battery manager interest notification");
      IOObjectRelease(service);
      continue;
    }

    gManagerService = service;
    log_message("info", "battery manager service matched");
    reconcile("manager-matched");
  }
}

static void load_configuration(void) {
  const char *threshold = getenv("AUTO_LOW_POWER_THRESHOLD");
  if (threshold != NULL && threshold[0] != '\0') {
    int value = atoi(threshold);
    if (value >= 0 && value <= 100) {
      gThreshold = value;
    }
  }

  const char *debug = getenv("AUTO_LOW_POWER_DEBUG");
  gDebug = (debug != NULL && (strcmp(debug, "1") == 0 || strcasecmp(debug, "true") == 0));

  const char *cooldown = getenv("AUTO_LOW_POWER_APPLY_COOLDOWN_SECONDS");
  if (cooldown != NULL && cooldown[0] != '\0') {
    double value = atof(cooldown);
    if (value >= 0.0) {
      gApplyCooldownSeconds = value;
    }
  }
}

int main(void) {
  load_configuration();

  gNotificationPort = IONotificationPortCreate(kIOMainPortDefault);
  if (gNotificationPort == NULL) {
    log_message("error", "failed to create IOKit notification port");
    return 1;
  }

  CFRunLoopSourceRef source = IONotificationPortGetRunLoopSource(gNotificationPort);
  if (source == NULL) {
    log_message("error", "failed to get IOKit run loop source");
    return 1;
  }

  CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);

  kern_return_t batteryStatus = IOServiceAddMatchingNotification(
    gNotificationPort,
    kIOMatchedNotification,
    IOServiceNameMatching("AppleSmartBattery"),
    battery_matched_callback,
    NULL,
    &gBatteryIterator
  );

  if (batteryStatus != KERN_SUCCESS) {
    log_message("error", "failed to add AppleSmartBattery matching notification");
    return 1;
  }

  kern_return_t managerStatus = IOServiceAddMatchingNotification(
    gNotificationPort,
    kIOMatchedNotification,
    IOServiceNameMatching("AppleSmartBatteryManager"),
    manager_matched_callback,
    NULL,
    &gManagerIterator
  );

  if (managerStatus != KERN_SUCCESS) {
    log_message("error", "failed to add AppleSmartBatteryManager matching notification");
    return 1;
  }

  battery_matched_callback(NULL, gBatteryIterator);
  manager_matched_callback(NULL, gManagerIterator);
  log_message("info", "listening for AppleSmartBattery and AppleSmartBatteryManager changes");
  CFRunLoopRun();
  return 0;
}
