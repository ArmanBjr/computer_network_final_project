#include "fsx/crypto/rsa.h"
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <cstring>
#include <stdexcept>

namespace fsx::crypto {

static constexpr int RSA_BITS = 2048;

// -----------------------------------------------------------------------------
// RsaKeyPair (server: generate, export public, decrypt)
// -----------------------------------------------------------------------------

RsaKeyPair::RsaKeyPair() : pkey_(nullptr) {}

RsaKeyPair::~RsaKeyPair() {
  if (pkey_) {
    EVP_PKEY_free(pkey_);
    pkey_ = nullptr;
  }
}

void RsaKeyPair::generate() {
  if (pkey_) {
    EVP_PKEY_free(pkey_);
    pkey_ = nullptr;
  }
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (!ctx) throw std::runtime_error("RsaKeyPair: EVP_PKEY_CTX_new_id failed");
  if (EVP_PKEY_keygen_init(ctx) != 1) {
    EVP_PKEY_CTX_free(ctx);
    throw std::runtime_error("RsaKeyPair: keygen_init failed");
  }
  if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, RSA_BITS) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    throw std::runtime_error("RsaKeyPair: set_rsa_keygen_bits failed");
  }
  if (EVP_PKEY_keygen(ctx, &pkey_) != 1) {
    EVP_PKEY_CTX_free(ctx);
    throw std::runtime_error("RsaKeyPair: keygen failed");
  }
  EVP_PKEY_CTX_free(ctx);
}

std::vector<uint8_t> RsaKeyPair::get_public_der() const {
  if (!pkey_) return {};
  unsigned char* p = nullptr;
  int len = i2d_PUBKEY(pkey_, &p);
  if (len <= 0 || !p) return {};
  std::vector<uint8_t> der(p, p + len);
  OPENSSL_free(p);
  return der;
}

std::vector<uint8_t> RsaKeyPair::decrypt(const uint8_t* ciphertext, size_t ciphertext_len) const {
  if (!pkey_ || !ciphertext || ciphertext_len == 0) return {};
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey_, nullptr);
  if (!ctx) return {};
  if (EVP_PKEY_decrypt_init(ctx) != 1) {
    EVP_PKEY_CTX_free(ctx);
    return {};
  }
  if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return {};
  }
  size_t outlen = 0;
  if (EVP_PKEY_decrypt(ctx, nullptr, &outlen, ciphertext, ciphertext_len) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return {};
  }
  std::vector<uint8_t> plaintext(outlen);
  if (EVP_PKEY_decrypt(ctx, plaintext.data(), &outlen, ciphertext, ciphertext_len) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return {};
  }
  plaintext.resize(outlen);
  EVP_PKEY_CTX_free(ctx);
  return plaintext;
}

// -----------------------------------------------------------------------------
// RsaPublicKey (client: load from DER, encrypt)
// -----------------------------------------------------------------------------

RsaPublicKey::RsaPublicKey() : pkey_(nullptr) {}

RsaPublicKey::~RsaPublicKey() {
  if (pkey_) {
    EVP_PKEY_free(pkey_);
    pkey_ = nullptr;
  }
}

bool RsaPublicKey::load_from_der(const uint8_t* der, size_t der_len) {
  if (pkey_) {
    EVP_PKEY_free(pkey_);
    pkey_ = nullptr;
  }
  if (!der || der_len == 0) return false;
  const unsigned char* p = der;
  pkey_ = d2i_PUBKEY(nullptr, &p, static_cast<long>(der_len));
  return (pkey_ != nullptr && p == der + der_len);
}

std::vector<uint8_t> RsaPublicKey::encrypt(const uint8_t* plaintext, size_t plaintext_len) const {
  if (!pkey_ || !plaintext || plaintext_len == 0) return {};
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey_, nullptr);
  if (!ctx) return {};
  if (EVP_PKEY_encrypt_init(ctx) != 1) {
    EVP_PKEY_CTX_free(ctx);
    return {};
  }
  if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return {};
  }
  size_t outlen = 0;
  if (EVP_PKEY_encrypt(ctx, nullptr, &outlen, plaintext, plaintext_len) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return {};
  }
  std::vector<uint8_t> ciphertext(outlen);
  if (EVP_PKEY_encrypt(ctx, ciphertext.data(), &outlen, plaintext, plaintext_len) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return {};
  }
  ciphertext.resize(outlen);
  EVP_PKEY_CTX_free(ctx);
  return ciphertext;
}

} // namespace fsx::crypto
