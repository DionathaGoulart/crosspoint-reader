#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>
#include <ProfilePath.h>

#include <cstdint>
#include <string>
#include <vector>

// Who is reading, so two people can share one device without sharing a figure.
//
// This store holds the policy -- which profiles exist, what they are called,
// which one is active -- and pushes the active id down to profilePath, which is
// the only thing the storage layer needs to resolve a per-profile file. The
// store itself is device-wide: it must be readable before anyone knows which
// profile to read.
//
// The default profile is reserved and cannot be deleted. Its files keep the
// names they have always had, so a card that predates profiles already is the
// default profile and nothing has to be moved.
struct Profile {
  uint8_t id = profilePath::DEFAULT_ID;
  // Display only, and never part of a path: a typed name would have to be
  // validated as a path component, and the id already identifies the files.
  // Empty means the screen shows its own translated label, which is how the
  // default profile stays named in the reader's language.
  std::string name;
};

class ProfileStore : public PersistableStore<ProfileStore> {
 public:
  // Eight is the width of the settings list without scrolling and far past what
  // a shared e-reader needs. Each one costs a directory, not RAM.
  static constexpr size_t MAX_PROFILES = 8;
  // Long enough for a first name; bounded because it is drawn in a list row.
  static constexpr size_t MAX_NAME_LENGTH = 24;

 private:
  // Always holds the default profile at index 0, created on first load.
  std::vector<Profile> profiles;
  uint8_t active = profilePath::DEFAULT_ID;

  ProfileStore() = default;
  ~ProfileStore() = default;

  friend class PersistableStore<ProfileStore>;

  // Smallest id not in use. The default id is never returned.
  uint8_t allocateId() const;
  // Drops a profile's whole directory. Called when a profile is deleted, and
  // again before a reused id is handed out, because an id freed by a delete
  // whose directory removal failed must not hand its files to the next person.
  static bool wipeProfileData(uint8_t id);

 public:
  static const char* getFilePath() { return "/.crosspoint/profiles.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Applies the stored choice to profilePath. Call once at boot, after
  // loadFromFile() and before any other store loads, or those stores read the
  // default profile's files whatever was chosen.
  void begin();

  const std::vector<Profile>& all() const { return profiles; }
  uint8_t activeId() const { return active; }
  size_t count() const { return profiles.size(); }
  // True while only the default profile exists, which is when the reader has
  // never used the feature and nothing about the UI should change.
  bool isSingleProfile() const { return profiles.size() <= 1; }
  const Profile* find(uint8_t id) const;

  // Adds a profile and persists. Fails when the name is empty, already taken,
  // or MAX_PROFILES is reached. Does not switch to it.
  bool create(const std::string& name, uint8_t* newId = nullptr);
  bool rename(uint8_t id, const std::string& name);
  // Deletes the profile and its data. Refuses the default profile. Switches to
  // the default first when the active profile is the one going.
  bool remove(uint8_t id);
  // Switches the active profile and persists the choice. The caller is
  // responsible for reloading the stores that follow the reader; this only
  // moves where they will read from.
  bool setActive(uint8_t id);
};

#define PROFILES ProfileStore::getInstance()
