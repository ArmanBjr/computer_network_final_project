#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <arpa/inet.h>

namespace fsx::protocol {

// ONLINE_LIST_REQ: empty payload

// ONLINE_LIST_RESP payload format:
// u16 count (network order)
// for each user:
//   u16 username_len (network order)
//   bytes username

struct OnlineListResp {
  std::vector<std::string> usernames;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> out;
    size_t est_size = 2 + usernames.size() * 64; // rough estimate
    out.reserve(est_size);

    uint16_t count_be = htons(static_cast<uint16_t>(usernames.size()));
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&count_be),
               reinterpret_cast<const uint8_t*>(&count_be) + 2);

    for (const auto& username : usernames) {
      uint16_t len_be = htons(static_cast<uint16_t>(username.size()));
      out.insert(out.end(), reinterpret_cast<const uint8_t*>(&len_be),
                 reinterpret_cast<const uint8_t*>(&len_be) + 2);
      out.insert(out.end(), username.begin(), username.end());
    }

    return out;
  }

  static OnlineListResp deserialize(const std::vector<uint8_t>& payload) {
    OnlineListResp resp;
    if (payload.size() < 2) return resp;

    size_t pos = 0;
    uint16_t count = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;

    resp.usernames.reserve(count);
    for (uint16_t i = 0; i < count && pos + 2 <= payload.size(); i++) {
      uint16_t len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
      pos += 2;
      if (pos + len <= payload.size()) {
        std::string username(reinterpret_cast<const char*>(payload.data() + pos), len);
        resp.usernames.push_back(std::move(username));
        pos += len;
      }
    }

    return resp;
  }
};

} // namespace fsx::protocol

