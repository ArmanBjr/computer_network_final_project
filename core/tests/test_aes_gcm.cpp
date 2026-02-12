// Unit test for AesGcm (Phase 8: encryption)
#include "fsx/crypto/aes_gcm.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>

int main() {
  std::cout << "Testing AesGcm::encrypt / decrypt...\n";

  uint8_t key[fsx::crypto::AES256_KEY_SIZE];
  fsx::crypto::AesGcm::random_key(key);

  // Test 1: Round-trip with auto nonce (prepended)
  std::string plain = "Hello Phase 8 AES-GCM";
  std::vector<uint8_t> plainvec(plain.begin(), plain.end());
  auto encrypted = fsx::crypto::AesGcm::encrypt(plainvec, key, nullptr);
  std::cout << "Test 1: encrypt(\"Hello Phase 8...\") -> " << encrypted.size() << " bytes (nonce+cipher+tag)\n";
  assert(encrypted.size() >= fsx::crypto::GCM_NONCE_SIZE + fsx::crypto::GCM_TAG_SIZE + plainvec.size());
  auto decrypted = fsx::crypto::AesGcm::decrypt(encrypted, key);
  assert(decrypted.size() == plainvec.size());
  assert(memcmp(decrypted.data(), plainvec.data(), plainvec.size()) == 0);
  std::cout << "  decrypt -> OK\n";
  std::cout << "  PASS\n\n";

  // Test 2: Empty plaintext
  std::vector<uint8_t> empty;
  auto enc_empty = fsx::crypto::AesGcm::encrypt(empty, key, nullptr);
  assert(enc_empty.size() >= fsx::crypto::GCM_NONCE_SIZE + fsx::crypto::GCM_TAG_SIZE);
  auto dec_empty = fsx::crypto::AesGcm::decrypt(enc_empty, key);
  assert(dec_empty.empty());
  std::cout << "Test 2: encrypt(empty) round-trip: PASS\n\n";

  // Test 3: Decrypt with wrong key -> empty (auth failure)
  uint8_t wrong_key[fsx::crypto::AES256_KEY_SIZE];
  fsx::crypto::AesGcm::random_key(wrong_key);
  auto dec_wrong = fsx::crypto::AesGcm::decrypt(encrypted, wrong_key);
  assert(dec_wrong.empty());
  std::cout << "Test 3: decrypt with wrong key -> empty (auth fail): PASS\n\n";

  // Test 4: Fixed nonce (caller-provided) round-trip — decrypt expects nonce+cipher+tag
  uint8_t nonce[fsx::crypto::GCM_NONCE_SIZE];
  fsx::crypto::AesGcm::random_nonce(nonce);
  auto enc_fixed = fsx::crypto::AesGcm::encrypt(plainvec, key, nonce);
  assert(enc_fixed.size() == plainvec.size() + fsx::crypto::GCM_TAG_SIZE);  // no prepended nonce
  std::vector<uint8_t> enc_with_nonce;
  enc_with_nonce.insert(enc_with_nonce.end(), nonce, nonce + fsx::crypto::GCM_NONCE_SIZE);
  enc_with_nonce.insert(enc_with_nonce.end(), enc_fixed.begin(), enc_fixed.end());
  auto dec_fixed = fsx::crypto::AesGcm::decrypt(enc_with_nonce, key);
  assert(dec_fixed.size() == plainvec.size());
  assert(memcmp(dec_fixed.data(), plainvec.data(), plainvec.size()) == 0);
  std::cout << "Test 4: encrypt with fixed nonce, decrypt(nonce+cipher+tag): PASS\n\n";

  std::cout << "All AesGcm tests passed!\n";
  return 0;
}
