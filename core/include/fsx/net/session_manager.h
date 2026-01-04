#pragma once

#include "fsx/net/tcp_session.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>

namespace fsx::net {

class SessionManager {
 public:
  void add_session(const std::string& token, std::shared_ptr<TcpSession> session);
  void remove_session(const std::string& token);
  void remove_session(std::shared_ptr<TcpSession> session);

  std::vector<std::string> get_online_usernames() const;
  size_t count() const;

 private:
  mutable std::mutex mutex_;
  mutable std::unordered_map<std::string, std::weak_ptr<TcpSession>> sessions_; // token -> session
};

} // namespace fsx::net

