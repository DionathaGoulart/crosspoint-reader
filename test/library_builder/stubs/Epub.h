#pragma once

#include <map>
#include <optional>
#include <string>

#include "HalStorage.h"

struct FakeMetadata {
  std::string title = "Title";
  std::string author = "Author";
  bool success = true;
  std::string series;
  std::optional<float> seriesIndex;
  std::string genre;
};

inline std::map<std::string, FakeMetadata> bookMetadata;

class Epub {
  std::string path;

 public:
  Epub(const std::string& path, const char*) : path(path) {}

  bool loadMetadata(std::string& title, std::string& author, std::string* series = nullptr,
                    std::optional<float>* seriesIndex = nullptr, std::string* genre = nullptr) {
    ++fake::parses;
    const auto& metadata = bookMetadata[path];
    if (!metadata.success) return false;
    title = metadata.title;
    author = metadata.author;
    if (series) *series = metadata.series;
    if (seriesIndex) *seriesIndex = metadata.seriesIndex;
    if (genre) *genre = metadata.genre;
    return true;
  }
};
