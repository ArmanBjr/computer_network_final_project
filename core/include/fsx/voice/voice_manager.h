#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>
#include <boost/asio.hpp>

namespace fsx::voice {

/// One active voice call between two users.
struct VoiceSession {
  uint32_t session_id = 0;

  // Caller
  long long caller_user_id = 0;
  std::string caller_username;
  boost::asio::ip::udp::endpoint caller_udp;  // learned from first UDP frame
  bool caller_udp_known = false;

  // Callee
  long long callee_user_id = 0;
  std::string callee_username;
  boost::asio::ip::udp::endpoint callee_udp;
  bool callee_udp_known = false;

  // Stats
  uint64_t frames_relayed = 0;
};

/**
 * Manages voice call sessions.
 *
 * Lifecycle:
 *   1. TCP: VOICE_CALL_REQ  → start_session() → returns session_id
 *   2. UDP: first frame from each side registers their endpoint
 *   3. UDP: relay_frame() forwards data to the other side
 *   4. TCP: VOICE_END        → end_session()
 */
class VoiceManager {
public:
  VoiceManager();
  ~VoiceManager();

  /// Create a new voice session (called on VOICE_CALL accepted).
  uint32_t start_session(long long caller_uid, const std::string& caller_name,
                         long long callee_uid, const std::string& callee_name);

  /// End a voice session.
  bool end_session(uint32_t session_id);

  /// Get session info (nullptr if not found).
  std::shared_ptr<VoiceSession> get_session(uint32_t session_id);

  /// Register the UDP endpoint for a participant (learned from first frame).
  /// Returns true if the endpoint was new.
  bool register_endpoint(uint32_t session_id,
                         const boost::asio::ip::udp::endpoint& ep);

  /**
   * Given a received UDP frame's session_id and sender endpoint,
   * return the endpoint of the OTHER participant (relay target).
   * Returns nullopt if not ready or session not found.
   */
  std::optional<boost::asio::ip::udp::endpoint>
  get_relay_target(uint32_t session_id,
                   const boost::asio::ip::udp::endpoint& sender_ep);

  /// Increment relay counter.
  void record_relayed(uint32_t session_id);

  /// Get all active sessions (for dashboard).
  std::vector<std::shared_ptr<VoiceSession>> get_all_sessions();

  /// Number of active sessions.
  size_t count() const;

private:
  std::atomic<uint32_t> next_id_{1};
  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, std::shared_ptr<VoiceSession>> sessions_;
};

} // namespace fsx::voice
