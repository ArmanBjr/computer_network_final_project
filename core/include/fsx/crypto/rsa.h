#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <openssl/evp.h>

namespace fsx::crypto {

// RSA 2048 for Phase 8: encrypt session key (32 bytes) with server public key
// Server holds key pair; client imports public key and encrypts session key

class RsaKeyPair {
public:
  RsaKeyPair();
  ~RsaKeyPair();

  // Generate 2048-bit RSA key pair
  void generate();

  // Export public key as DER (to send to client)
  std::vector<uint8_t> get_public_der() const;

  // Decrypt ciphertext (e.g. RSA-encrypted session key). Returns plaintext or empty on failure.
  std::vector<uint8_t> decrypt(const uint8_t* ciphertext, size_t ciphertext_len) const;
  std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext) const {
    if (ciphertext.empty()) return {};
    return decrypt(ciphertext.data(), ciphertext.size());
  }

  bool is_initialized() const { return pkey_ != nullptr; }

private:
  EVP_PKEY* pkey_ = nullptr;
};

// Client side: load server's public key and encrypt session key
class RsaPublicKey {
public:
  RsaPublicKey();
  ~RsaPublicKey();

  // Load public key from DER (received from server)
  bool load_from_der(const uint8_t* der, size_t der_len);
  bool load_from_der(const std::vector<uint8_t>& der) {
    if (der.empty()) return false;
    return load_from_der(der.data(), der.size());
  }

  // Encrypt plaintext (e.g. 32-byte session key). Returns ciphertext or empty on failure.
  std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t plaintext_len) const;
  std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext) const {
    if (plaintext.empty()) return {};
    return encrypt(plaintext.data(), plaintext.size());
  }

  bool is_loaded() const { return pkey_ != nullptr; }

private:
  EVP_PKEY* pkey_ = nullptr;
};

} // namespace fsx::crypto
