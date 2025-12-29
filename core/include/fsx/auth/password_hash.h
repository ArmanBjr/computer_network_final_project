#pragma once

#include <string>

namespace fsx::auth {

// format: pbkdf2$<iters>$<salt_hex>$<hash_hex>
std::string hash_password_pbkdf2(const std::string& password, int iters = 120000);

bool verify_password_pbkdf2(const std::string& password, const std::string& stored);

} // namespace fsx::auth

