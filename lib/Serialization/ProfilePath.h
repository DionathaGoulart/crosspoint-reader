#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Where a per-profile file lives on the card.
 *
 * This layer knows only that there is an active profile id and how to turn a
 * file name into a path for it. Which profiles exist, what they are called and
 * how one is chosen is policy, and lives in src/profiles/ProfileStore.h, which
 * pushes the active id down here at boot and on every switch. Keeping the split
 * that way is what lets PersistableStore resolve a path without lib/ depending
 * on src/.
 *
 * The default profile keeps the historical file names, so a card that predates
 * profiles is already the default profile and nothing has to be moved.
 */
namespace profilePath {

// The profile an existing card already is. Reserved: it cannot be deleted and
// its files are never under profiles/.
constexpr uint8_t DEFAULT_ID = 0;

// Set at boot from the stored choice, and on every switch. Device-wide files
// ignore it.
void setActiveId(uint8_t id);
uint8_t activeId();

// Absolute path for `fileName` -- a bare name like "recent.json", or a name
// ending in '/' for a directory. With `perProfile` false, or for the default
// profile, this is "/.crosspoint/<fileName>"; otherwise
// "/.crosspoint/profiles/<id>/<fileName>".
//
// The id is a number, never a profile's name: a name is typed by a person and
// would have to be validated as a path component, and nothing good comes of
// putting user text in a path.
std::string resolve(const char* fileName, bool perProfile);

// The active profile's directory, with a trailing '/', or "/.crosspoint/" for
// the default profile. For callers that build their own file names under it.
std::string activeDir(bool perProfile);

// Creates the active profile's directory if it is missing, one level at a time
// because mkdir is not recursive. Call before the first write of a run; a read
// does not need it. True when the directory exists afterwards.
bool ensureActiveDir(bool perProfile);

}  // namespace profilePath
