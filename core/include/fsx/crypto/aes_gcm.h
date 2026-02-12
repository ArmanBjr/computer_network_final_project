#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace fsx::crypto {

// AES-256-GCM for Phase 8: encrypt/decrypt payloads (e.g. FILE_CHUNK)
// Key: 32 bytes; Nonce: 12 bytes (must be unique per encryption with same key); Tag: 16 bytes

static constexpr size_t AES256_KEY_SIZE = 32;
static constexpr size_t GCM_NONCE_SIZE  = 12;
static constexpr size_t GCM_TAG_SIZE    = 16;

class AesGcm {
public:
  // Encrypt plaintext with key and nonce. Output: nonce (12) + ciphertext + tag (16).
  // Caller can provide nonce (must be unique) or leave empty to generate random.
  static std::vector<uint8_t> encrypt(
    const void* plaintext,
    size_t plaintext_len,
    const uint8_t key[AES256_KEY_SIZE],
    const uint8_t nonce[GCM_NONCE_SIZE] = nullptr  // if null, generate random and prepend to output
  );

  static std::vector<uint8_t> encrypt(
    const std::vector<uint8_t>& plaintext,
    const uint8_t key[AES256_KEY_SIZE],
    const uint8_t nonce[GCM_NONCE_SIZE] = nullptr
  ) {
    if (plaintext.empty()) return encrypt(nullptr, 0, key, nonce);
    return encrypt(plaintext.data(), plaintext.size(), key, nonce);
  }

  // Decrypt: input is nonce (12) + ciphertext + tag (16). Returns plaintext or empty on auth failure.
  static std::vector<uint8_t> decrypt(
    const void* encrypted,
    size_t encrypted_len,
    const uint8_t key[AES256_KEY_SIZE]
  );

  static std::vector<uint8_t> decrypt(
    const std::vector<uint8_t>& encrypted,
    const uint8_t key[AES256_KEY_SIZE]
  ) {
    if (encrypted.size() < GCM_NONCE_SIZE + GCM_TAG_SIZE) return {};
    return decrypt(encrypted.data(), encrypted.size(), key);
  }

  // Generate a random nonce (12 bytes). Uses OpenSSL RAND_bytes.
  static void random_nonce(uint8_t nonce[GCM_NONCE_SIZE]);

  // Generate random AES-256 key (32 bytes).
  static void random_key(uint8_t key[AES256_KEY_SIZE]);
};

} // namespace fsx::crypto
