// Unit test for IntegrityService: CRC32 (Phase 4) + SHA-256 (Phase 7)

#include "fsx/transfer/integrity.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>

// Standard CRC32 test vector: "123456789" should produce 0xCBF43926
static constexpr uint32_t EXPECTED_CRC32 = 0xCBF43926UL;

// SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
static const unsigned char SHA256_ABC_HEX[] = {
  0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
  0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
  0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
  0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
};

static bool sha256_equal_vec(const std::vector<uint8_t>& a, const unsigned char* b, size_t n) {
  if (a.size() != n) return false;
  return std::memcmp(a.data(), b, n) == 0;
}

int main() {
  std::cout << "Testing IntegrityService::crc32()...\n";
  
  // Test 1: Standard test vector "123456789"
  const char* test_string = "123456789";
  uint32_t crc = fsx::transfer::IntegrityService::crc32(test_string, strlen(test_string));
  
  std::cout << "Test 1: CRC32(\"123456789\") = 0x" << std::hex << crc << std::dec << "\n";
  std::cout << "Expected: 0x" << std::hex << EXPECTED_CRC32 << std::dec << "\n";
  
  if (crc != EXPECTED_CRC32) {
    std::cerr << "FAIL: CRC32 mismatch!\n";
    return 1;
  }
  std::cout << "✓ PASS\n\n";
  
  // Test 2: Empty string should return 0
  uint32_t crc_empty = fsx::transfer::IntegrityService::crc32("", 0);
  std::cout << "Test 2: CRC32(\"\") = 0x" << std::hex << crc_empty << std::dec << "\n";
  if (crc_empty != 0) {
    std::cerr << "FAIL: Empty string should return 0\n";
    return 1;
  }
  std::cout << "✓ PASS\n\n";
  
  // Test 3: std::vector<uint8_t> overload
  std::vector<uint8_t> test_vec = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39}; // "123456789"
  uint32_t crc_vec = fsx::transfer::IntegrityService::crc32(test_vec);
  std::cout << "Test 3: CRC32(vector) = 0x" << std::hex << crc_vec << std::dec << "\n";
  if (crc_vec != EXPECTED_CRC32) {
    std::cerr << "FAIL: Vector overload mismatch!\n";
    return 1;
  }
  std::cout << "✓ PASS\n\n";
  
  // Test 4: std::string overload
  std::string test_str = "123456789";
  uint32_t crc_str = fsx::transfer::IntegrityService::crc32(test_str);
  std::cout << "Test 4: CRC32(string) = 0x" << std::hex << crc_str << std::dec << "\n";
  if (crc_str != EXPECTED_CRC32) {
    std::cerr << "FAIL: String overload mismatch!\n";
    return 1;
  }
  std::cout << "✓ PASS\n\n";
  
  // Test 5: Different data should produce different CRC
  const char* test_string2 = "987654321";
  uint32_t crc2 = fsx::transfer::IntegrityService::crc32(test_string2, strlen(test_string2));
  std::cout << "Test 5: CRC32(\"987654321\") = 0x" << std::hex << crc2 << std::dec << "\n";
  if (crc2 == EXPECTED_CRC32) {
    std::cerr << "FAIL: Different data should produce different CRC!\n";
    return 1;
  }
  std::cout << "✓ PASS\n\n";

  // --- Phase 7: SHA-256 tests ---
  std::cout << "Testing IntegrityService::sha256() (Phase 7)...\n";

  // Test 6: SHA-256("abc") standard vector
  const char* abc = "abc";
  auto sha_abc = fsx::transfer::IntegrityService::sha256(abc, 3);
  if (sha_abc.size() != fsx::transfer::IntegrityService::SHA256_SIZE) {
    std::cerr << "FAIL: SHA-256(\"abc\") wrong size " << sha_abc.size() << "\n";
    return 1;
  }
  if (!sha256_equal_vec(sha_abc, SHA256_ABC_HEX, sizeof(SHA256_ABC_HEX))) {
    std::cerr << "FAIL: SHA-256(\"abc\") digest mismatch\n";
    return 1;
  }
  std::cout << "Test 6: SHA-256(\"abc\") ✓ PASS\n\n";

  // Test 7: sha256_equal
  std::vector<uint8_t> abc_vec(sha_abc.begin(), sha_abc.end());
  if (!fsx::transfer::IntegrityService::sha256_equal(sha_abc, abc_vec)) {
    std::cerr << "FAIL: sha256_equal same digest\n";
    return 1;
  }
  abc_vec[0] ^= 1;
  if (fsx::transfer::IntegrityService::sha256_equal(sha_abc, abc_vec)) {
    std::cerr << "FAIL: sha256_equal different digest\n";
    return 1;
  }
  std::cout << "Test 7: sha256_equal ✓ PASS\n\n";

  // Test 8: sha256_file (create temp file with "abc", hash it)
  const char* tmp_name = "/tmp/fsx_sha256_test_abc.txt";
  std::ofstream f(tmp_name, std::ios::binary);
  if (f) {
    f.write(abc, 3);
    f.close();
    auto file_hash = fsx::transfer::IntegrityService::sha256_file(tmp_name);
    if (file_hash.size() != fsx::transfer::IntegrityService::SHA256_SIZE ||
        !sha256_equal_vec(file_hash, SHA256_ABC_HEX, sizeof(SHA256_ABC_HEX))) {
      std::cerr << "FAIL: sha256_file digest mismatch\n";
      return 1;
    }
    std::cout << "Test 8: sha256_file ✓ PASS\n\n";
  } else {
    std::cout << "Test 8: sha256_file skipped (cannot create " << tmp_name << ")\n\n";
  }
  
  std::cout << "All tests passed! ✓\n";
  return 0;
}

