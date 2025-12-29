#include "fsx/auth/password_hash.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <stdexcept>

namespace fsx::auth {

static std::string to_hex(const unsigned char* data, size_t len) {
  std::ostringstream ss;
  for (size_t i = 0; i < len; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
  return ss.str();
}

static std::vector<unsigned char> from_hex(const std::string& hex) {
  if (hex.size() % 2 != 0) throw std::runtime_error("invalid hex");
  std::vector<unsigned char> out(hex.size()/2);
  for (size_t i=0;i<out.size();i++) {
    unsigned int v;
    std::stringstream ss;
    ss << std::hex << hex.substr(2*i,2);
    ss >> v;
    out[i] = (unsigned char)v;
  }
  return out;
}

std::string hash_password_pbkdf2(const std::string& password, int iters) {
  const size_t salt_len = 16;
  const size_t dk_len = 32; // 256-bit

  unsigned char salt[salt_len];
  if (RAND_bytes(salt, (int)salt_len) != 1) throw std::runtime_error("RAND_bytes(salt) failed");

  unsigned char dk[dk_len];
  if (PKCS5_PBKDF2_HMAC(password.c_str(), (int)password.size(),
                        salt, (int)salt_len,
                        iters, EVP_sha256(),
                        (int)dk_len, dk) != 1) {
    throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");
  }

  std::ostringstream ss;
  ss << "pbkdf2$" << iters << "$" << to_hex(salt, salt_len) << "$" << to_hex(dk, dk_len);
  return ss.str();
}

bool verify_password_pbkdf2(const std::string& password, const std::string& stored) {
  // stored: pbkdf2$iters$salt_hex$hash_hex
  auto p1 = stored.find('$');
  if (p1 == std::string::npos) return false;
  auto p2 = stored.find('$', p1 + 1);
  if (p2 == std::string::npos) return false;
  auto p3 = stored.find('$', p2 + 1);
  if (p3 == std::string::npos) return false;

  std::string scheme = stored.substr(0, p1);
  if (scheme != "pbkdf2") return false;

  int iters = std::stoi(stored.substr(p1 + 1, p2 - (p1 + 1)));
  std::string salt_hex = stored.substr(p2 + 1, p3 - (p2 + 1));
  std::string hash_hex = stored.substr(p3 + 1);

  auto salt = from_hex(salt_hex);
  auto hash = from_hex(hash_hex);

  std::vector<unsigned char> dk(hash.size());
  if (PKCS5_PBKDF2_HMAC(password.c_str(), (int)password.size(),
                        salt.data(), (int)salt.size(),
                        iters, EVP_sha256(),
                        (int)dk.size(), dk.data()) != 1) {
    return false;
  }

  // constant-time compare
  if (dk.size() != hash.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < dk.size(); i++) diff |= (dk[i] ^ hash[i]);
  return diff == 0;
}

} // namespace fsx::auth

