#pragma once
// Stand-in for lib/Serialization/PersistableStore.h, backed by the fake
// filesystem from test/library_builder/stubs/HalStorage.h.
//
// loadFromFile() deliberately reproduces one behaviour of the real base that
// matters to the tests: when the file does not exist it returns false WITHOUT
// calling fromJson(), so whatever is in memory survives. That is what makes
// FinishedBooksStore::reset() necessary on a profile switch, and the test for
// it would pass against a stub that cleared instead.
#include <ArduinoJson.h>
#include <HalStorage.h>

#include <string>

struct PersistableStoreBase {
  static bool writeDocToFile(const char* path, const JsonDocument& doc) {
    std::string json;
    serializeJson(doc, json);
    fake::add(path, json);
    return true;
  }
  static bool readDocFromFile(const char* path, JsonDocument& doc) {
    const auto found = fake::files.find(path);
    if (found == fake::files.end()) return false;
    return !deserializeJson(doc, std::string(found->second->bytes.begin(), found->second->bytes.end()));
  }
  static bool profileFileExists(const char* fileName, bool /*perProfile*/) {
    return fake::files.find(fileName) != fake::files.end();
  }
};

template <typename T>
class PersistableStore : public PersistableStoreBase {
 public:
  static T& getInstance() {
    static T instance;
    return instance;
  }

  bool saveToFile() {
    JsonDocument doc;
    static_cast<const T*>(this)->toJson(doc);
    return writeDocToFile(T::getFilePath(), doc);
  }

  bool loadFromFile() {
    JsonDocument doc;
    if (!readDocFromFile(T::getFilePath(), doc)) return false;  // fromJson() is NOT reached
    resaveRequested = false;
    const bool ok = static_cast<T*>(this)->fromJson(doc.as<JsonVariantConst>());
    if (ok && resaveRequested) saveToFile();
    resaveRequested = false;
    return ok;
  }

  // Exposed so a test can assert that a format migration asked to be rewritten.
  bool resaveWasRequested() const { return resaveRequested; }

 protected:
  void requestResave() { resaveRequested = true; }

 private:
  bool resaveRequested = false;
};
