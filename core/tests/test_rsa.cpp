// Unit test for RSA key exchange (Phase 8)
#include "fsx/crypto/rsa.h"
#include "fsx/crypto/aes_gcm.h"
#include <iostream>
#include <cassert>
#include <cstring>

int main() {
  std::cout << "Testing RSA key exchange (Phase 8)...\n";

  // Server: generate key pair
  fsx::crypto::RsaKeyPair server_key;
  server_key.generate();
  assert(server_key.is_initialized());
  std::cout << "Test 1: Server generated RSA key pair: OK\n";

  auto pub_der = server_key.get_public_der();
  assert(!pub_der.empty());
  std::cout << "Test 2: get_public_der() -> " << pub_der.size() << " bytes: OK\n";

  // Client: load public key and encrypt 32-byte session key
  fsx::crypto::RsaPublicKey client_pub;
  assert(client_pub.load_from_der(pub_der));
  std::cout << "Test 3: Client loaded public key from DER: OK\n";

  uint8_t session_key[32];
  fsx::crypto::AesGcm::random_key(session_key);
  auto encrypted = client_pub.encrypt(session_key, 32);
  assert(!encrypted.empty());
  std::cout << "Test 4: Client encrypted 32-byte session key -> " << encrypted.size() << " bytes: OK\n";

  // Server: decrypt and verify
  auto decrypted = server_key.decrypt(encrypted);
  assert(decrypted.size() == 32);
  assert(memcmp(decrypted.data(), session_key, 32) == 0);
  std::cout << "Test 5: Server decrypted session key, match: OK\n";

  std::cout << "All RSA key exchange tests passed!\n";
  return 0;
}
