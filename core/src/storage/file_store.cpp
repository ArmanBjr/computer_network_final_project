#include "fsx/storage/file_store.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <iostream>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#endif

namespace fsx::storage {

FileStore::FileStore(const std::string& base_storage_path)
  : base_path_(base_storage_path) {
}

FileStore::~FileStore() {
}

bool FileStore::initialize() {
  return ensure_directory(base_path_);
}

bool FileStore::ensure_directory(const std::string& path) {
  try {
    std::filesystem::create_directories(path);
    return true;
  } catch (const std::exception&) {
    // Fallback to C API
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
      // Directory doesn't exist, create it
      if (mkdir(path.c_str(), 0755) != 0) {
        return false;
      }
    } else if (!(info.st_mode & S_IFDIR)) {
      // Path exists but is not a directory
      return false;
    }
    return true;
  }
}

void* FileStore::open_for_write(uint64_t transfer_id, const std::string& filename) {
  std::string temp_path = get_temp_path(transfer_id, filename);
  
  // Ensure transfer directory exists
  std::string transfer_dir = base_path_ + "/" + std::to_string(transfer_id);
  if (!ensure_directory(transfer_dir)) {
    std::cerr << "[FileStore] Failed to create directory: " << transfer_dir << "\n";
    std::cerr.flush();
    return nullptr;
  }
  
  // Open file in binary write mode
  FILE* f = fopen(temp_path.c_str(), "wb");
  if (!f) {
    std::cerr << "[FileStore] Failed to open file for writing: " << temp_path << " (errno: " << errno << ")\n";
    std::cerr.flush();
    return nullptr;
  }
  
  std::cout << "[FileStore] Opened file for writing: " << temp_path << "\n";
  std::cout.flush();
  return reinterpret_cast<void*>(f);
}

int64_t FileStore::write_chunk(void* file_handle, const void* data, size_t len) {
  if (!file_handle || !data || len == 0) {
    std::cerr << "[FileStore] write_chunk: invalid parameters (handle=" << file_handle << " data=" << data << " len=" << len << ")\n";
    std::cerr.flush();
    return -1;
  }
  
  FILE* f = reinterpret_cast<FILE*>(file_handle);
  size_t written = fwrite(data, 1, len, f);
  if (written != len) {
    std::cerr << "[FileStore] write_chunk: partial write (written=" << written << " expected=" << len << " errno=" << errno << ")\n";
    std::cerr.flush();
    return -1;
  }
  
  // Flush to ensure data is written
  if (fflush(f) != 0) {
    std::cerr << "[FileStore] write_chunk: fflush failed (errno=" << errno << ")\n";
    std::cerr.flush();
  }
  
  return static_cast<int64_t>(written);
}

bool FileStore::finalize_file(uint64_t transfer_id, const std::string& filename, void* file_handle) {
  if (!file_handle) {
    return false;
  }
  
  FILE* f = reinterpret_cast<FILE*>(file_handle);
  fclose(f);
  
  std::string temp_path = get_temp_path(transfer_id, filename);
  std::string final_path = get_file_path(transfer_id, filename);
  
  // Rename .part to final filename
  if (rename(temp_path.c_str(), final_path.c_str()) != 0) {
    return false;
  }
  
  return true;
}

std::string FileStore::get_file_path(uint64_t transfer_id, const std::string& filename) const {
  std::ostringstream oss;
  oss << base_path_ << "/" << transfer_id << "/" << filename;
  return oss.str();
}

std::string FileStore::get_temp_path(uint64_t transfer_id, const std::string& filename) const {
  std::ostringstream oss;
  oss << base_path_ << "/" << transfer_id << "/" << filename << ".part";
  return oss.str();
}

bool FileStore::cleanup_transfer(uint64_t transfer_id) {
  std::string transfer_dir = base_path_ + "/" + std::to_string(transfer_id);
  try {
    std::filesystem::remove_all(transfer_dir);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace fsx::storage

