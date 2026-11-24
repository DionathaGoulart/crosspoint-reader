#pragma once
// Minimal stand-in: ProfilePath only calls mkdir/exists, and the path
// composition under test does not touch storage at all.
struct FakeStorage {
  bool mkdir(const char*, bool = true) { return true; }
  bool exists(const char*) { return true; }
};
inline FakeStorage Storage;
