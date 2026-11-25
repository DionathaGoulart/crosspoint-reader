#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>

#include <mutex>
#include <string>
#include <type_traits>

#include "ProfilePath.h"

/**
 * @brief Non-template core of PersistableStore.
 *
 * All ArduinoJson parse/serialize machinery is instantiated once here (in
 * PersistableStore.cpp) instead of in every store's translation unit. GCC
 * emits the JSON serializer/parser templates as local .isra clones per TU
 * (~0.5KB each), so keeping serializeJson/deserializeJson out of the stores
 * is what makes the abstraction flash-neutral.
 */
class PersistableStoreBase {
 protected:
  PersistableStoreBase() = default;
  ~PersistableStoreBase() = default;

  // Serializes saveToFile/loadFromFile against each other across FreeRTOS
  // tasks, so the JSON snapshot cannot tear mid-serialize and two concurrent
  // saves cannot write their documents out of order. Concurrent saves are
  // reachable: the web server task saves settings while the main task can too.
  //
  // It is deliberately held across the SD write. That is safe only because the
  // read path does NOT take it — derived stores build their snapshots (e.g.
  // CrossPointSettings::statusBarSpec) unlocked. If you ever lock this mutex on
  // a read path, you put it on the render path and stall rendering behind SD
  // I/O, and you create a storeMutex/storageMutex ordering hazard. Don't.
  mutable std::mutex storeMutex;

  // fromJson() implementations call this (instead of saveToFile()) when the
  // on-disk JSON used a legacy shape that was upgraded in memory.
  // loadFromFile() performs the save after releasing storeMutex; calling
  // saveToFile() from inside fromJson() would deadlock on storeMutex.
  void requestResave() { resaveRequested = true; }

  bool resaveRequested = false;

 public:
  // Public so non-store JSON files (e.g. per-book bookmarks) can reuse them
  // instead of instantiating serializeJson/deserializeJson in their own TU —
  // that per-TU duplication is exactly what this class exists to prevent.

  // Serializes doc and writes it to path (ensures /.crosspoint exists). Logs on failure.
  static bool writeDocToFile(const char* path, const JsonDocument& doc);

  // Reads path and parses it into doc. Returns false silently when the file
  // does not exist (expected on first boot); logs on read/parse failure.
  static bool readDocFromFile(const char* path, JsonDocument& doc);

  // The two above, with the path resolved for the active profile first. They
  // are not templates so the resolution and the std::string it needs exist
  // once, for the same reason serializeJson lives in this class rather than in
  // every store: a per-store copy is what this abstraction exists to avoid.
  static bool writeDocForProfile(const char* fileName, bool perProfile, const JsonDocument& doc);
  static bool readDocForProfile(const char* fileName, bool perProfile, JsonDocument& doc);
  // Whether the store's file exists for the active profile. A lazy loader uses
  // it to tell "no file yet" (an empty store) from "file unreadable" (which
  // must not be saved over).
  static bool profileFileExists(const char* fileName, bool perProfile);

 protected:
  /**
   * Helper function for extracting an obfuscated password from a JSON value.
   * Accepts JsonVariantConst so callers can pass either a whole JsonDocument
   * or a JsonObject element (e.g. inside an array iteration).
   * If the decoded password requires a resave (e.g. from plaintext fallback), `needsResave` is set to true.
   */
  static std::string extractPassword(JsonVariantConst doc, bool& needsResave);
  static std::string extractPassword(JsonVariantConst doc, bool& needsResave, size_t maxLength, bool& valid);
};

/**
 * @brief Base class for persistable singletons using CRTP.
 *
 * Derived classes must provide:
 * - A private default constructor
 * - friend class PersistableStore<Derived>;
 * - static const char* getFilePath();
 * - void toJson(JsonDocument& doc) const;
 * - bool fromJson(JsonVariantConst doc);
 *
 * Note for implementers: read string values as `const char*` (e.g.
 * `obj["name"] | ""`), never as `| std::string("")` — ArduinoJson's
 * std::string converter drags a per-TU copy of the whole JSON serializer
 * into flash via its serializeJson fallback.
 *
 * Concurrency: saveToFile/loadFromFile lock storeMutex, so toJson/fromJson
 * always run under it. fromJson must signal legacy-shape upgrades with
 * requestResave(), never by calling saveToFile() directly (deadlock).
 */
/**
 * @brief Whether a store's file belongs to the reader rather than the device.
 *
 * A store opts in with `static constexpr bool kPerProfile = true;`. Saying
 * nothing means device-wide, which is the safe default: a file that should have
 * been split shows up as two readers sharing a figure, while a file that should
 * not have been split can mean a device that loses its Wi-Fi or its LCP key on
 * a profile switch.
 */
template <typename T, typename = void>
struct StoreIsPerProfile : std::false_type {};
template <typename T>
struct StoreIsPerProfile<T, std::void_t<decltype(T::kPerProfile)>> : std::bool_constant<T::kPerProfile> {};

template <typename T>
class PersistableStore : public PersistableStoreBase {
 protected:
  PersistableStore() = default;
  ~PersistableStore() = default;

 public:
  // Delete copy constructor and assignment
  PersistableStore(const PersistableStore&) = delete;
  PersistableStore& operator=(const PersistableStore&) = delete;

  static T& getInstance() {
    static T instance;
    return instance;
  }

  bool saveToFile() const {
    std::lock_guard<std::mutex> lock(storeMutex);
    JsonDocument doc;
    static_cast<const T*>(this)->toJson(doc);
    return writeDocForProfile(T::getFilePath(), StoreIsPerProfile<T>::value, doc);
  }

  bool loadFromFile() {
    bool ok;
    bool doResave;
    {
      std::lock_guard<std::mutex> lock(storeMutex);
      resaveRequested = false;
      JsonDocument doc;
      if (!readDocForProfile(T::getFilePath(), StoreIsPerProfile<T>::value, doc)) {
        return false;
      }
      ok = static_cast<T*>(this)->fromJson(doc.as<JsonVariantConst>());
      // Read the flag under the lock that guards the fromJson() that set it.
      doResave = resaveRequested;
      resaveRequested = false;
    }
    // Deliberately outside the lock: saveToFile() takes storeMutex itself.
    if (ok && doResave && !saveToFile()) {
      LOG_ERR("PERSIST", "Failed to resave %s after format update", T::getFilePath());
    }
    return ok;
  }
};
