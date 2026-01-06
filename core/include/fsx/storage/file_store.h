#pragma once

#include <cstdint>
#include <string>
#include <memory>

namespace fsx::storage {

class FileStore {
public:
  FileStore(const std::string& base_storage_path = "./storage/transfers");
  ~FileStore();

  // Initialize storage directory structure
  bool initialize();

  // Open a file for writing (creates .part file)
  // Returns file handle (FILE*) or nullptr on error
  void* open_for_write(uint64_t transfer_id, const std::string& filename);

  // Write chunk data to file
  // Returns bytes written, or -1 on error
  int64_t write_chunk(void* file_handle, const void* data, size_t len);

  // Finalize file: close, rename .part to final filename
  // Returns true on success
  bool finalize_file(uint64_t transfer_id, const std::string& filename, void* file_handle);

  // Get full path for a transfer file
  std::string get_file_path(uint64_t transfer_id, const std::string& filename) const;
  std::string get_temp_path(uint64_t transfer_id, const std::string& filename) const;

  // Cleanup: remove transfer directory (optional, for failed transfers)
  bool cleanup_transfer(uint64_t transfer_id);

private:
  std::string base_path_;
  
  // Ensure directory exists
  bool ensure_directory(const std::string& path);
};

} // namespace fsx::storage

