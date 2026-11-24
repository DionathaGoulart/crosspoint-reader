#pragma once
#include <string>

// The real digest is irrelevant here: these tests address books by the id they
// hand in, and never compare one against a file's contents.
class KOReaderDocumentId {
 public:
  static std::string calculate(const std::string& filePath) { return filePath; }
  static std::string calculateFromFilename(const std::string& filePath) { return filePath; }
};
