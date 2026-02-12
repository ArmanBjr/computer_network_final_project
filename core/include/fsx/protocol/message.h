#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <arpa/inet.h>

namespace fsx::protocol {

static constexpr uint32_t MAGIC = 0x46535831; // "FSX1"
static constexpr uint8_t  VERSION = 1;
static constexpr size_t   HEADER_SIZE = 12;

enum class MsgType : uint8_t {
  HELLO = 1,
  PING  = 2,
  PONG  = 3,
  // Auth messages
  REGISTER_REQ  = 10,
  REGISTER_RESP = 11,
  LOGIN_REQ     = 12,
  LOGIN_RESP    = 13,
  // Online list
  ONLINE_LIST_REQ  = 20,
  ONLINE_LIST_RESP = 21,
  // File transfer messages (Phase 3)
  FILE_OFFER_REQ   = 30,
  FILE_OFFER_RESP  = 31,
  FILE_ACCEPT_REQ  = 32,
  FILE_ACCEPT_RESP = 33,
  FILE_CHUNK       = 34,  // Phase 3 (deprecated, use FILE_UPLOAD_CHUNK)
  FILE_DONE        = 35,
  FILE_RESULT      = 36,
  // Upload messages with CRC32 (Phase 4)
  FILE_UPLOAD_CHUNK = 37,
  FILE_UPLOAD_ACK   = 38,
  FILE_UPLOAD_NAK   = 39,
  // Download messages (Phase 4)
  FILE_DOWNLOAD_REQ    = 40,
  FILE_DOWNLOAD_START  = 41,
  FILE_DOWNLOAD_CHUNK  = 42,
  FILE_DOWNLOAD_ACK    = 43,
  FILE_DOWNLOAD_NAK    = 44,
  FILE_DOWNLOAD_DONE   = 45,
  // Resume messages (Phase 5)
  RESUME_QUERY  = 50,
  RESUME_REPLY  = 51,
  // Phase 8: Key exchange (RSA handshake)
  KEY_EXCHANGE_PUBKEY     = 60,  // server -> client: public key DER
  KEY_EXCHANGE_SESSION_KEY = 61, // client -> server: RSA-encrypted 32-byte session key
  FILE_UPLOAD_CHUNK_ENCRYPTED = 62, // client -> server: AES-GCM encrypted FileUploadChunk payload (nonce+cipher+tag)
  // Phase 9: Throttle & transfer-list (admin)
  THROTTLE_SET       = 70,   // admin -> server: set rate limit
  THROTTLE_SET_RESP  = 71,   // server -> admin: ack
  TRANSFER_LIST_REQ  = 72,   // admin -> server: list active transfers
  TRANSFER_LIST_RESP = 73,   // server -> admin: transfer list

  // Phase 10: Voice chat signaling
  VOICE_CALL_REQ    = 80,   // client -> server: request voice call
  VOICE_CALL_RESP   = 81,   // server -> caller / callee -> server: accept/reject
  VOICE_CALL_NOTIFY = 82,   // server -> callee: incoming call notification
  VOICE_END         = 83,   // both: end voice call
  // Phase 11: Voice session list (admin/dashboard)
  VOICE_SESSION_LIST_REQ  = 84,
  VOICE_SESSION_LIST_RESP = 85,

  // Admin messages (port 9100)
  ADMIN_ONLINE_LIST_REQ  = 100,
  ADMIN_ONLINE_LIST_RESP = 101
};

#pragma pack(push, 1)
struct MessageHeaderWire {
  uint32_t magic_be;   // network order
  uint8_t  version;
  uint8_t  type;
  uint32_t len_be;     // network order
  uint16_t reserved_be; // network order (0 for now)
};
#pragma pack(pop)

struct Message {
  MsgType type;
  std::vector<uint8_t> payload;
};

inline MessageHeaderWire make_header(MsgType type, uint32_t len) {
  MessageHeaderWire h{};
  h.magic_be = htonl(MAGIC);
  h.version  = VERSION;
  h.type     = static_cast<uint8_t>(type);
  h.len_be   = htonl(len);
  h.reserved_be = htons(0);
  return h;
}

inline void validate_header(const MessageHeaderWire& h) {
  if (ntohl(h.magic_be) != MAGIC) throw std::runtime_error("bad magic");
  if (h.version != VERSION) throw std::runtime_error("bad version");
}

inline uint32_t payload_len(const MessageHeaderWire& h) {
  return ntohl(h.len_be);
}

} // namespace fsx::protocol
