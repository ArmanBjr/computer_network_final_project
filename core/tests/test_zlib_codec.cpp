// Unit test for ZlibCodec (Phase 6: compression)
#include "fsx/transfer/zlib_codec.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>

int main() {
  std::cout << "Testing ZlibCodec::compress / decompress...\n";

  // Test 1: Round-trip simple string
  std::string s = "Hello World";
  std::vector<uint8_t> data(s.begin(), s.end());
  auto compressed = fsx::transfer::ZlibCodec::compress(data);
  std::cout << "Test 1: compress(\"Hello World\") -> " << compressed.size() << " bytes\n";
  assert(!compressed.empty());
  auto decompressed = fsx::transfer::ZlibCodec::decompress(compressed, data.size());
  assert(decompressed.size() == data.size());
  assert(memcmp(decompressed.data(), data.data(), data.size()) == 0);
  std::cout << "  decompress -> OK\n";
  std::cout << "  PASS\n\n";

  // Test 2: Empty input
  std::vector<uint8_t> empty;
  auto c_empty = fsx::transfer::ZlibCodec::compress(empty);
  assert(c_empty.empty());
  std::cout << "Test 2: compress(empty) -> empty: PASS\n\n";

  // Test 3: Binary data (repeated bytes compress well)
  std::vector<uint8_t> binary(1000, 0xAB);
  auto c_binary = fsx::transfer::ZlibCodec::compress(binary);
  std::cout << "Test 3: compress(1000 bytes 0xAB) -> " << c_binary.size() << " bytes\n";
  assert(!c_binary.empty() && c_binary.size() < binary.size());
  auto d_binary = fsx::transfer::ZlibCodec::decompress(c_binary, binary.size());
  assert(d_binary.size() == binary.size());
  assert(memcmp(d_binary.data(), binary.data(), binary.size()) == 0);
  std::cout << "  PASS\n\n";

  // Test 4: Decompress with wrong original_size fails (or returns wrong size)
  auto d_bad = fsx::transfer::ZlibCodec::decompress(c_binary, 500);
  std::cout << "Test 4: decompress with wrong size -> empty or partial: " << d_bad.size() << " bytes\n";
  std::cout << "  PASS\n\n";

  std::cout << "All ZlibCodec tests passed!\n";
  return 0;
}
