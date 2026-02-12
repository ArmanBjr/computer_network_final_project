#include "fsx/crypto/aes_gcm.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <cstring>
#include <stdexcept>

namespace fsx::crypto {

void AesGcm::random_nonce(uint8_t nonce[GCM_NONCE_SIZE]) {
  if (RAND_bytes(nonce, GCM_NONCE_SIZE) != 1) {
    throw std::runtime_error("AesGcm: RAND_bytes nonce failed");
  }
}

void AesGcm::random_key(uint8_t key[AES256_KEY_SIZE]) {
  if (RAND_bytes(key, AES256_KEY_SIZE) != 1) {
    throw std::runtime_error("AesGcm: RAND_bytes key failed");
  }
}

std::vector<uint8_t> AesGcm::encrypt(
  const void* plaintext,
  size_t plaintext_len,
  const uint8_t key[AES256_KEY_SIZE],
  const uint8_t nonce[GCM_NONCE_SIZE]
) {
  uint8_t actual_nonce[GCM_NONCE_SIZE];
  bool prepend_nonce = (nonce == nullptr);
  if (prepend_nonce) {
    random_nonce(actual_nonce);
  } else {
    std::memcpy(actual_nonce, nonce, GCM_NONCE_SIZE);
  }

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    throw std::runtime_error("AesGcm: EVP_CIPHER_CTX_new failed");
  }

  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("AesGcm: EncryptInit failed");
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_NONCE_SIZE, nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("AesGcm: SET_IVLEN failed");
  }
  if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, actual_nonce) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("AesGcm: EncryptInit key/iv failed");
  }

  std::vector<uint8_t> out(plaintext_len + GCM_TAG_SIZE);
  int outlen = 0;
  if (plaintext_len > 0 && plaintext != nullptr) {
    if (EVP_EncryptUpdate(ctx, out.data(), &outlen, static_cast<const uint8_t*>(plaintext), static_cast<int>(plaintext_len)) != 1) {
      EVP_CIPHER_CTX_free(ctx);
      throw std::runtime_error("AesGcm: EncryptUpdate failed");
    }
  }
  int tmplen = 0;
  if (EVP_EncryptFinal_ex(ctx, out.data() + outlen, &tmplen) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("AesGcm: EncryptFinal failed");
  }
  outlen += tmplen;

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_SIZE, out.data() + outlen) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("AesGcm: GET_TAG failed");
  }
  outlen += GCM_TAG_SIZE;
  out.resize(outlen);

  EVP_CIPHER_CTX_free(ctx);

  std::vector<uint8_t> result;
  if (prepend_nonce) {
    result.reserve(GCM_NONCE_SIZE + out.size());
    result.insert(result.end(), actual_nonce, actual_nonce + GCM_NONCE_SIZE);
    result.insert(result.end(), out.begin(), out.end());
  } else {
    result = std::move(out);
  }
  return result;
}

std::vector<uint8_t> AesGcm::decrypt(
  const void* encrypted,
  size_t encrypted_len,
  const uint8_t key[AES256_KEY_SIZE]
) {
  if (encrypted_len < GCM_NONCE_SIZE + GCM_TAG_SIZE || encrypted == nullptr) {
    return {};
  }

  const uint8_t* in = static_cast<const uint8_t*>(encrypted);
  const uint8_t* nonce = in;
  size_t cipher_len = encrypted_len - GCM_NONCE_SIZE - GCM_TAG_SIZE;
  const uint8_t* cipher = in + GCM_NONCE_SIZE;
  const uint8_t* tag = in + GCM_NONCE_SIZE + cipher_len;

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return {};

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_NONCE_SIZE, nullptr) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_SIZE, const_cast<uint8_t*>(tag)) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};
  }

  std::vector<uint8_t> plaintext(cipher_len);
  int outlen = 0;
  if (cipher_len > 0) {
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &outlen, cipher, static_cast<int>(cipher_len)) != 1) {
      EVP_CIPHER_CTX_free(ctx);
      return {};
    }
  }
  int tmplen = 0;
  if (EVP_DecryptFinal_ex(ctx, plaintext.data() + outlen, &tmplen) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {};  // Auth failure (tag mismatch)
  }
  outlen += tmplen;
  plaintext.resize(outlen);
  EVP_CIPHER_CTX_free(ctx);
  return plaintext;
}

} // namespace fsx::crypto
