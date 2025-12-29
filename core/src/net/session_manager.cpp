#include "fsx/net/session_manager.h"
#include <algorithm>

namespace fsx::net {

void SessionManager::add_session(const std::string& token, std::shared_ptr<TcpSession> session) {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_[token] = session;
}

void SessionManager::remove_session(const std::string& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.erase(token);
}

void SessionManager::remove_session(std::shared_ptr<TcpSession> session) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (auto s = it->second.lock()) {
      if (s == session) {
        it = sessions_.erase(it);
        return;
      }
      ++it;
    } else {
      // Clean up expired weak_ptr
      it = sessions_.erase(it);
    }
  }
}

std::vector<std::string> SessionManager::get_online_usernames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> usernames;
  
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (auto session = it->second.lock()) {
      if (session->is_authenticated()) {
        usernames.push_back(session->username());
      }
      ++it;
    } else {
      // Clean up expired weak_ptr
      it = sessions_.erase(it);
    }
  }
  
  return usernames;
}

size_t SessionManager::count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t cnt = 0;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (auto s = it->second.lock()) {
      if (s->is_authenticated()) cnt++;
      ++it;
    } else {
      it = sessions_.erase(it);
    }
  }
  return cnt;
}

} // namespace fsx::net

