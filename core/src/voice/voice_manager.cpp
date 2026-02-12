#include "fsx/voice/voice_manager.h"
#include <iostream>

namespace fsx::voice {

VoiceManager::VoiceManager() = default;
VoiceManager::~VoiceManager() = default;

uint32_t VoiceManager::start_session(long long caller_uid, const std::string& caller_name,
                                     long long callee_uid, const std::string& callee_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint32_t sid = next_id_.fetch_add(1);

  auto s = std::make_shared<VoiceSession>();
  s->session_id = sid;
  s->caller_user_id = caller_uid;
  s->caller_username = caller_name;
  s->callee_user_id = callee_uid;
  s->callee_username = callee_name;

  sessions_[sid] = s;
  std::cout << "[voice] session " << sid << " started: "
            << caller_name << " -> " << callee_name << "\n";
  std::cout.flush();
  return sid;
}

bool VoiceManager::end_session(uint32_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return false;

  std::cout << "[voice] session " << session_id << " ended ("
            << it->second->frames_relayed << " frames relayed)\n";
  std::cout.flush();
  sessions_.erase(it);
  return true;
}

std::shared_ptr<VoiceSession> VoiceManager::get_session(uint32_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  return it != sessions_.end() ? it->second : nullptr;
}

bool VoiceManager::register_endpoint(uint32_t session_id,
                                     const boost::asio::ip::udp::endpoint& ep) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return false;

  auto& s = it->second;

  // If this endpoint matches neither side yet, assign to the first free slot.
  // We determine "which side" by checking if the endpoint is already known.
  if (!s->caller_udp_known) {
    s->caller_udp = ep;
    s->caller_udp_known = true;
    std::cout << "[voice] session " << session_id
              << " caller UDP registered: " << ep << "\n";
    std::cout.flush();
    return true;
  }
  if (s->caller_udp == ep) return false;  // already known

  if (!s->callee_udp_known) {
    s->callee_udp = ep;
    s->callee_udp_known = true;
    std::cout << "[voice] session " << session_id
              << " callee UDP registered: " << ep << "\n";
    std::cout.flush();
    return true;
  }
  return false;  // both known
}

std::optional<boost::asio::ip::udp::endpoint>
VoiceManager::get_relay_target(uint32_t session_id,
                               const boost::asio::ip::udp::endpoint& sender_ep) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return std::nullopt;

  auto& s = it->second;

  if (s->caller_udp_known && s->caller_udp == sender_ep && s->callee_udp_known) {
    return s->callee_udp;
  }
  if (s->callee_udp_known && s->callee_udp == sender_ep && s->caller_udp_known) {
    return s->caller_udp;
  }

  return std::nullopt;
}

void VoiceManager::record_relayed(uint32_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    it->second->frames_relayed++;
  }
}

std::vector<std::shared_ptr<VoiceSession>> VoiceManager::get_all_sessions() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::shared_ptr<VoiceSession>> result;
  result.reserve(sessions_.size());
  for (auto& [id, s] : sessions_) {
    result.push_back(s);
  }
  return result;
}

size_t VoiceManager::count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

} // namespace fsx::voice
