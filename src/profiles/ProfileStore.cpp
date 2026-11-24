#include "ProfileStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

void ProfileStore::toJson(JsonDocument& doc) const {
  doc["active"] = active;
  JsonArray array = doc["profiles"].to<JsonArray>();
  for (const Profile& profile : profiles) {
    JsonObject object = array.add<JsonObject>();
    object["id"] = profile.id;
    object["name"] = profile.name;
  }
}

bool ProfileStore::fromJson(const JsonVariantConst doc) {
  profiles.clear();
  // A missing or malformed array is treated as "no profiles yet" rather than a
  // failure: the default profile is seeded below either way, and refusing to
  // load would leave a reader with no profile at all.
  for (JsonVariantConst entry : doc["profiles"].as<JsonArrayConst>()) {
    if (profiles.size() >= MAX_PROFILES) break;
    Profile profile;
    profile.id = entry["id"] | profilePath::DEFAULT_ID;
    // const char* rather than std::string: ArduinoJson's std::string converter
    // drags a copy of the whole serializer into this translation unit.
    profile.name = entry["name"] | "";
    if (profile.name.size() > MAX_NAME_LENGTH) profile.name.resize(MAX_NAME_LENGTH);
    // Two entries claiming one id would make two profiles share a directory.
    if (find(profile.id) != nullptr) continue;
    profiles.push_back(std::move(profile));
  }

  if (find(profilePath::DEFAULT_ID) == nullptr) {
    // Index 0 by convention, so the screen lists the original reader first.
    profiles.insert(profiles.begin(), Profile{profilePath::DEFAULT_ID, std::string()});
  }

  active = doc["active"] | profilePath::DEFAULT_ID;
  // A choice pointing at a profile that is gone would read a directory nobody
  // owns. Falling back is silent on purpose: it is what a half-written file or
  // a hand-edited card looks like, and the default profile is always there.
  if (find(active) == nullptr) active = profilePath::DEFAULT_ID;
  return true;
}

void ProfileStore::begin() {
  // Seeded here as well as in fromJson(), because PersistableStore skips
  // fromJson() entirely when the file is missing -- which is every device that
  // has never created a profile. Without this the list would be empty and the
  // settings screen would show no profiles at all.
  if (find(profilePath::DEFAULT_ID) == nullptr) {
    profiles.insert(profiles.begin(), Profile{profilePath::DEFAULT_ID, std::string()});
    active = profilePath::DEFAULT_ID;
  }
  profilePath::setActiveId(active);
}

const Profile* ProfileStore::find(const uint8_t id) const {
  const auto it = std::find_if(profiles.begin(), profiles.end(), [id](const Profile& p) { return p.id == id; });
  return it == profiles.end() ? nullptr : &*it;
}

uint8_t ProfileStore::allocateId() const {
  for (uint8_t candidate = profilePath::DEFAULT_ID + 1; candidate < profilePath::DEFAULT_ID + MAX_PROFILES + 1;
       ++candidate) {
    if (find(candidate) == nullptr) return candidate;
  }
  return profilePath::DEFAULT_ID;
}

bool ProfileStore::wipeProfileData(const uint8_t id) {
  if (id == profilePath::DEFAULT_ID) return false;  // that is the whole card
  const uint8_t previous = profilePath::activeId();
  profilePath::setActiveId(id);
  std::string dir = profilePath::activeDir(/*perProfile=*/true);
  profilePath::setActiveId(previous);
  if (!dir.empty() && dir.back() == '/') dir.pop_back();
  if (!Storage.exists(dir.c_str())) return true;
  if (Storage.removeDir(dir.c_str())) return true;
  LOG_ERR("PROFILE", "Could not remove %s", dir.c_str());
  return false;
}

bool ProfileStore::create(const std::string& name, uint8_t* newId) {
  if (name.empty() || name.size() > MAX_NAME_LENGTH) return false;
  if (profiles.size() >= MAX_PROFILES) return false;
  // Two profiles with one name is not ambiguous to the firmware, which uses
  // ids, but it is to the person choosing between them in a list.
  if (std::any_of(profiles.begin(), profiles.end(), [&](const Profile& p) { return p.name == name; })) return false;

  const uint8_t id = allocateId();
  if (id == profilePath::DEFAULT_ID) return false;
  // An id is only reused after a delete, and a delete whose directory removal
  // failed would otherwise hand the old reader's statistics to the new one.
  wipeProfileData(id);

  profiles.push_back(Profile{id, name});
  if (!saveToFile()) {
    profiles.pop_back();
    return false;
  }
  if (newId != nullptr) *newId = id;
  return true;
}

bool ProfileStore::rename(const uint8_t id, const std::string& name) {
  if (name.empty() || name.size() > MAX_NAME_LENGTH) return false;
  const auto it = std::find_if(profiles.begin(), profiles.end(), [id](const Profile& p) { return p.id == id; });
  if (it == profiles.end()) return false;
  if (std::any_of(profiles.begin(), profiles.end(),
                  [&](const Profile& p) { return p.id != id && p.name == name; })) {
    return false;
  }
  std::string previous = std::move(it->name);
  it->name = name;
  if (saveToFile()) return true;
  it->name = std::move(previous);
  return false;
}

bool ProfileStore::remove(const uint8_t id) {
  if (id == profilePath::DEFAULT_ID) return false;
  const auto it = std::find_if(profiles.begin(), profiles.end(), [id](const Profile& p) { return p.id == id; });
  if (it == profiles.end()) return false;

  // Leave the active profile before its files go, so nothing in flight writes
  // into a directory that is being removed.
  if (active == id && !setActive(profilePath::DEFAULT_ID)) return false;

  const Profile removed = *it;
  profiles.erase(it);
  if (!saveToFile()) {
    profiles.push_back(removed);
    return false;
  }
  // The list is what the reader sees, so a failure to remove the directory is
  // logged rather than rolled back: the profile is gone either way, and
  // create() wipes a reused id before handing it out.
  wipeProfileData(id);
  return true;
}

bool ProfileStore::setActive(const uint8_t id) {
  if (find(id) == nullptr) return false;
  if (active == id) return true;
  const uint8_t previous = active;
  active = id;
  if (!saveToFile()) {
    active = previous;
    return false;
  }
  profilePath::setActiveId(active);
  return true;
}
