#pragma once
#include <cstdint>

enum class DocumentMatchMethod : uint8_t { FILENAME = 0, BINARY = 1 };

class KOReaderCredentialStore {
 public:
  static KOReaderCredentialStore& getInstance() {
    static KOReaderCredentialStore store;
    return store;
  }
  DocumentMatchMethod getMatchMethod() const { return DocumentMatchMethod::FILENAME; }
};

#define KOREADER_STORE KOReaderCredentialStore::getInstance()
