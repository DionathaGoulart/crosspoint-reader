#pragma once

#include <string>

// Where the custom sleep images live, per profile.
//
// The default profile keeps the SD root, as it always has: /.sleep, /sleep,
// /sleep.bmp, /.sleep-overlay and so on. Every other profile has the same
// names inside its own folder, /.crosspoint/profiles/<id>/, so each reader
// keeps their own wallpapers and a new profile starts with none.
namespace sleepimages {

// The names of the custom sleep images, relative to rootDir(). Every place
// that reads or writes one takes its name from here.
inline constexpr char CUSTOM_BMP[] = "sleep.bmp";
inline constexpr char CUSTOM_DIR[] = ".sleep";
inline constexpr char CUSTOM_LEGACY_DIR[] = "sleep";
inline constexpr char OVERLAY_BMP[] = "sleep-overlay.bmp";
inline constexpr char OVERLAY_PNG[] = "sleep-overlay.png";
inline constexpr char OVERLAY_DIR[] = ".sleep-overlay";
inline constexpr char OVERLAY_LEGACY_DIR[] = "sleep-overlay";

// The folder the active profile's sleep images are under, with a trailing '/':
// "/" for the default profile, otherwise "/.crosspoint/profiles/<id>/".
std::string rootDir();

// rootDir() followed by `name`, one of the names above (or a temporary file
// beside one).
std::string path(const char* name);

// Creates the active profile's folder if it is missing, so an image can be
// written into it. True when the folder exists afterwards.
bool ensureRootDir();

// True for a path that is a custom sleep image (/sleep.bmp, or a file in
// /sleep or /.sleep) under the SD root or under any profile's folder. Used to
// drop converted copies when one of those files changes.
bool isSleepImagePath(const char* path);

}  // namespace sleepimages
