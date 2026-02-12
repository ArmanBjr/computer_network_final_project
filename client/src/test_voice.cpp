// Voice Chat Test Client (Phase 10)
// Usage:
//   call:   ./test_voice call   <username> <password> <callee_username> [host] [port] [duration_sec]
//   answer: ./test_voice answer <username> <password> [host] [port] [duration_sec]
//
// Both modes:
//   1. Connect TCP, consume KEY_EXCHANGE_PUBKEY, login
//   2. Caller sends VOICE_CALL_REQ, waits for VOICE_CALL_RESP
//      Answerer waits for VOICE_CALL_NOTIFY, sends VOICE_CALL_RESP(accept)
//   3. Both open UDP socket to server, exchange synthetic Opus frames for <duration_sec>
//   4. Caller sends VOICE_END

#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <arpa/inet.h>
#include <cmath>

// Opus codec for encoding synthetic audio
#include <opus/opus.h>

static constexpr uint32_t MAGIC   = 0x46535831; // FSX1
static constexpr uint8_t  VERSION = 1;

// Voice protocol constants
static constexpr int SAMPLE_RATE   = 48000;
static constexpr int CHANNELS      = 1;
static constexpr int FRAME_SAMPLES = 960; // 20ms at 48kHz
static constexpr int FRAME_MS      = 20;

// Message types
static constexpr uint8_t MSG_LOGIN_REQ         = 12;
static constexpr uint8_t MSG_LOGIN_RESP        = 13;
static constexpr uint8_t MSG_KEY_EXCHANGE_PUBKEY = 60;
static constexpr uint8_t MSG_VOICE_CALL_REQ    = 80;
static constexpr uint8_t MSG_VOICE_CALL_RESP   = 81;
static constexpr uint8_t MSG_VOICE_CALL_NOTIFY = 82;
static constexpr uint8_t MSG_VOICE_END         = 83;

#pragma pack(push, 1)
struct Header {
  uint32_t magic_be;
  uint8_t  version;
  uint8_t  type;
  uint32_t len_be;
  uint16_t reserved_be;
};
#pragma pack(pop)

// ------- helpers -------

static std::vector<uint8_t> make_frame(uint8_t type, const std::vector<uint8_t>& payload) {
  Header h{};
  h.magic_be    = htonl(MAGIC);
  h.version     = VERSION;
  h.type        = type;
  h.len_be      = htonl(static_cast<uint32_t>(payload.size()));
  h.reserved_be = htons(0);

  std::vector<uint8_t> out(sizeof(h) + payload.size());
  std::memcpy(out.data(), &h, sizeof(h));
  if (!payload.empty())
    std::memcpy(out.data() + sizeof(h), payload.data(), payload.size());
  return out;
}

static Header read_header(boost::asio::ip::tcp::socket& sock) {
  Header h{};
  boost::asio::read(sock, boost::asio::buffer(&h, sizeof(h)));
  if (ntohl(h.magic_be) != MAGIC) throw std::runtime_error("bad magic");
  if (h.version != VERSION)       throw std::runtime_error("bad version");
  return h;
}

static std::vector<uint8_t> read_payload(boost::asio::ip::tcp::socket& sock, uint32_t len) {
  std::vector<uint8_t> payload(len);
  if (len > 0) boost::asio::read(sock, boost::asio::buffer(payload));
  return payload;
}

static void consume_key_exchange_pubkey(boost::asio::ip::tcp::socket& sock) {
  Header h = read_header(sock);
  uint32_t len = ntohl(h.len_be);
  if (h.type == MSG_KEY_EXCHANGE_PUBKEY) {
    auto payload = read_payload(sock, len);
    std::cout << "[tcp] consumed KEY_EXCHANGE_PUBKEY (" << payload.size() << " bytes DER)\n";
  } else {
    auto payload = read_payload(sock, len);
    std::cout << "[tcp] unexpected first message type=" << (int)h.type << "\n";
  }
}

static std::vector<uint8_t> make_login_req(const std::string& user, const std::string& pass) {
  std::vector<uint8_t> p;
  uint16_t ul = htons(static_cast<uint16_t>(user.size()));
  p.insert(p.end(), reinterpret_cast<uint8_t*>(&ul), reinterpret_cast<uint8_t*>(&ul) + 2);
  p.insert(p.end(), user.begin(), user.end());
  uint16_t pl = htons(static_cast<uint16_t>(pass.size()));
  p.insert(p.end(), reinterpret_cast<uint8_t*>(&pl), reinterpret_cast<uint8_t*>(&pl) + 2);
  p.insert(p.end(), pass.begin(), pass.end());
  return make_frame(MSG_LOGIN_REQ, p);
}

static bool do_login(boost::asio::ip::tcp::socket& sock, const std::string& user, const std::string& pass) {
  auto frame = make_login_req(user, pass);
  boost::asio::write(sock, boost::asio::buffer(frame));
  std::cout << "[tcp] LOGIN_REQ sent for " << user << "\n";

  auto h = read_header(sock);
  auto payload = read_payload(sock, ntohl(h.len_be));
  if (h.type != MSG_LOGIN_RESP || payload.empty() || payload[0] != 1) {
    std::cerr << "[tcp] LOGIN FAILED\n";
    return false;
  }
  std::cout << "[tcp] LOGIN OK for " << user << "\n";
  return true;
}

// ------- UDP voice frame builder -------

struct UdpVoiceFrame {
  uint32_t session_id;
  uint32_t seq;
  uint32_t timestamp_ms;
  std::vector<uint8_t> opus_data;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> buf;
    uint32_t sid_be = htonl(session_id);
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&sid_be),
               reinterpret_cast<const uint8_t*>(&sid_be) + 4);
    uint32_t seq_be = htonl(seq);
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&seq_be),
               reinterpret_cast<const uint8_t*>(&seq_be) + 4);
    uint32_t ts_be = htonl(timestamp_ms);
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&ts_be),
               reinterpret_cast<const uint8_t*>(&ts_be) + 4);
    uint16_t olen_be = htons(static_cast<uint16_t>(opus_data.size()));
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&olen_be),
               reinterpret_cast<const uint8_t*>(&olen_be) + 2);
    buf.insert(buf.end(), opus_data.begin(), opus_data.end());
    return buf;
  }
};

// Generate a 20ms sine-wave PCM frame at 440 Hz
static void generate_sine_pcm(int16_t* pcm, int samples, uint32_t seq) {
  double freq = 440.0;
  double offset = seq * samples;
  for (int i = 0; i < samples; i++) {
    double t = (offset + i) / static_cast<double>(SAMPLE_RATE);
    pcm[i] = static_cast<int16_t>(16000.0 * std::sin(2.0 * M_PI * freq * t));
  }
}

// ------- voice stream -------

static void voice_udp_loop(
    const std::string& host, uint16_t udp_port,
    uint32_t session_id, int duration_sec,
    std::atomic<bool>& stop_flag,
    std::atomic<uint64_t>& frames_sent,
    std::atomic<uint64_t>& frames_received) {

  boost::asio::io_context ioc;
  boost::asio::ip::udp::socket udp_sock(ioc, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
  boost::asio::ip::udp::endpoint server_ep(
      boost::asio::ip::address::from_string(host), udp_port);

  // Set socket to non-blocking for receive
  udp_sock.non_blocking(true);

  // Create Opus encoder
  int err = 0;
  OpusEncoder* enc = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err);
  if (err != OPUS_OK) {
    std::cerr << "[udp] opus encoder create failed\n";
    return;
  }
  opus_encoder_ctl(enc, OPUS_SET_BITRATE(32000));

  uint32_t seq = 0;
  auto start = std::chrono::steady_clock::now();

  while (!stop_flag.load()) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
    if (elapsed >= duration_sec) break;

    // Encode a synthetic sine wave
    int16_t pcm[FRAME_SAMPLES];
    generate_sine_pcm(pcm, FRAME_SAMPLES, seq);

    uint8_t opus_buf[512];
    int nbytes = opus_encode(enc, pcm, FRAME_SAMPLES, opus_buf, sizeof(opus_buf));
    if (nbytes < 0) {
      std::cerr << "[udp] opus encode error\n";
      break;
    }

    // Build and send UDP frame
    UdpVoiceFrame vf;
    vf.session_id = session_id;
    vf.seq = seq;
    vf.timestamp_ms = static_cast<uint32_t>(seq * FRAME_MS);
    vf.opus_data.assign(opus_buf, opus_buf + nbytes);

    auto wire = vf.serialize();
    boost::system::error_code ec;
    udp_sock.send_to(boost::asio::buffer(wire), server_ep, 0, ec);
    if (!ec) {
      frames_sent++;
    }
    seq++;

    // Try to receive frames (non-blocking)
    uint8_t recv_buf[2048];
    boost::asio::ip::udp::endpoint sender_ep;
    while (true) {
      size_t n = udp_sock.receive_from(boost::asio::buffer(recv_buf), sender_ep, 0, ec);
      if (ec) break; // EAGAIN / would block
      if (n >= 14) {
        frames_received++;
      }
    }

    // Sleep ~20ms between frames
    std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_MS));
  }

  opus_encoder_destroy(enc);
  std::cout << "[udp] voice loop ended. sent=" << frames_sent.load()
            << " received=" << frames_received.load() << "\n";
}

// ------- caller mode -------

static int do_call(const std::string& user, const std::string& pass,
                   const std::string& callee, const std::string& host,
                   uint16_t tcp_port, int duration_sec) {
  boost::asio::io_context ioc;
  boost::asio::ip::tcp::socket sock(ioc);
  sock.connect(boost::asio::ip::tcp::endpoint(
      boost::asio::ip::address::from_string(host), tcp_port));
  std::cout << "[tcp] connected to " << host << ":" << tcp_port << "\n";

  consume_key_exchange_pubkey(sock);
  if (!do_login(sock, user, pass)) return 1;

  // Send VOICE_CALL_REQ: u16 callee_name_len + callee_name
  {
    std::vector<uint8_t> p;
    uint16_t cl = htons(static_cast<uint16_t>(callee.size()));
    p.insert(p.end(), reinterpret_cast<uint8_t*>(&cl), reinterpret_cast<uint8_t*>(&cl) + 2);
    p.insert(p.end(), callee.begin(), callee.end());
    auto frame = make_frame(MSG_VOICE_CALL_REQ, p);
    boost::asio::write(sock, boost::asio::buffer(frame));
    std::cout << "[tcp] VOICE_CALL_REQ sent to " << callee << "\n";
  }

  // Wait for VOICE_CALL_RESP
  auto h = read_header(sock);
  auto resp = read_payload(sock, ntohl(h.len_be));
  if (h.type != MSG_VOICE_CALL_RESP || resp.empty() || resp[0] != 1) {
    std::cerr << "[tcp] VOICE_CALL rejected or failed\n";
    return 1;
  }

  // Parse session_id and udp_port from response
  uint32_t session_id_be;
  std::memcpy(&session_id_be, resp.data() + 1, 4);
  uint32_t session_id = ntohl(session_id_be);

  uint16_t udp_port_be;
  std::memcpy(&udp_port_be, resp.data() + 5, 2);
  uint16_t udp_port = ntohs(udp_port_be);

  std::cout << "[tcp] VOICE_CALL accepted! session_id=" << session_id
            << " udp_port=" << udp_port << "\n";

  // Start UDP voice loop
  std::atomic<bool> stop_flag{false};
  std::atomic<uint64_t> frames_sent{0}, frames_received{0};

  std::thread udp_thread(voice_udp_loop, host, udp_port,
                         session_id, duration_sec,
                         std::ref(stop_flag), std::ref(frames_sent), std::ref(frames_received));

  // Wait for duration
  std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
  stop_flag = true;
  udp_thread.join();

  // Send VOICE_END
  {
    std::vector<uint8_t> p;
    uint32_t sid_be = htonl(session_id);
    p.insert(p.end(), reinterpret_cast<uint8_t*>(&sid_be), reinterpret_cast<uint8_t*>(&sid_be) + 4);
    auto frame = make_frame(MSG_VOICE_END, p);
    boost::asio::write(sock, boost::asio::buffer(frame));
    std::cout << "[tcp] VOICE_END sent\n";
  }

  std::cout << "\n=== CALLER SUMMARY ===\n";
  std::cout << "  session_id: " << session_id << "\n";
  std::cout << "  frames_sent: " << frames_sent.load() << "\n";
  std::cout << "  frames_received: " << frames_received.load() << "\n";
  std::cout << "  duration: " << duration_sec << "s\n";
  std::cout << "======================\n";

  return 0;
}

// ------- answerer mode -------

static int do_answer(const std::string& user, const std::string& pass,
                     const std::string& host, uint16_t tcp_port, int duration_sec) {
  boost::asio::io_context ioc;
  boost::asio::ip::tcp::socket sock(ioc);
  sock.connect(boost::asio::ip::tcp::endpoint(
      boost::asio::ip::address::from_string(host), tcp_port));
  std::cout << "[tcp] connected to " << host << ":" << tcp_port << "\n";

  consume_key_exchange_pubkey(sock);
  if (!do_login(sock, user, pass)) return 1;

  // Wait for VOICE_CALL_NOTIFY
  std::cout << "[tcp] waiting for incoming voice call...\n";
  auto h = read_header(sock);
  auto payload = read_payload(sock, ntohl(h.len_be));

  if (h.type != MSG_VOICE_CALL_NOTIFY) {
    std::cerr << "[tcp] expected VOICE_CALL_NOTIFY, got type=" << (int)h.type << "\n";
    return 1;
  }

  // Parse: u32 session_id + u16 caller_name_len + caller_name
  uint32_t session_id_be;
  std::memcpy(&session_id_be, payload.data(), 4);
  uint32_t session_id = ntohl(session_id_be);

  uint16_t caller_len_be;
  std::memcpy(&caller_len_be, payload.data() + 4, 2);
  uint16_t caller_len = ntohs(caller_len_be);
  std::string caller_name(payload.begin() + 6, payload.begin() + 6 + caller_len);

  std::cout << "[tcp] incoming call from " << caller_name
            << " session_id=" << session_id << "\n";

  // Accept the call: VOICE_CALL_RESP u8(1) + u32 session_id
  {
    std::vector<uint8_t> p;
    p.push_back(1); // accept
    uint32_t sid_be = htonl(session_id);
    p.insert(p.end(), reinterpret_cast<uint8_t*>(&sid_be), reinterpret_cast<uint8_t*>(&sid_be) + 4);
    auto frame = make_frame(MSG_VOICE_CALL_RESP, p);
    boost::asio::write(sock, boost::asio::buffer(frame));
    std::cout << "[tcp] VOICE_CALL_RESP(accept) sent\n";
  }

  // Read VOICE_CALL_RESP back from server with udp_port
  auto h2 = read_header(sock);
  auto resp2 = read_payload(sock, ntohl(h2.len_be));
  if (h2.type != MSG_VOICE_CALL_RESP || resp2.empty() || resp2[0] != 1) {
    std::cerr << "[tcp] unexpected response after accept\n";
    return 1;
  }

  uint16_t udp_port_be;
  std::memcpy(&udp_port_be, resp2.data() + 5, 2);
  uint16_t udp_port = ntohs(udp_port_be);

  std::cout << "[tcp] call active! udp_port=" << udp_port << "\n";

  // Start UDP voice loop
  std::atomic<bool> stop_flag{false};
  std::atomic<uint64_t> frames_sent{0}, frames_received{0};

  std::thread udp_thread(voice_udp_loop, host, udp_port,
                         session_id, duration_sec,
                         std::ref(stop_flag), std::ref(frames_sent), std::ref(frames_received));

  // Also listen for VOICE_END from TCP in parallel
  std::thread tcp_listen([&]() {
    try {
      while (!stop_flag.load()) {
        // Set socket to non-blocking for polling
        sock.non_blocking(true);
        uint8_t peek[1];
        boost::system::error_code ec;
        sock.receive(boost::asio::buffer(peek), boost::asio::ip::tcp::socket::message_peek, ec);
        if (ec) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        sock.non_blocking(false);
        auto hdr = read_header(sock);
        auto pl = read_payload(sock, ntohl(hdr.len_be));
        if (hdr.type == MSG_VOICE_END) {
          std::cout << "[tcp] received VOICE_END from caller\n";
          stop_flag = true;
          break;
        }
      }
    } catch (...) {
      stop_flag = true;
    }
  });

  // Wait for UDP thread
  udp_thread.join();
  stop_flag = true;
  tcp_listen.join();

  std::cout << "\n=== ANSWERER SUMMARY ===\n";
  std::cout << "  session_id: " << session_id << "\n";
  std::cout << "  frames_sent: " << frames_sent.load() << "\n";
  std::cout << "  frames_received: " << frames_received.load() << "\n";
  std::cout << "  duration: " << duration_sec << "s\n";
  std::cout << "========================\n";

  return 0;
}

// ------- main -------

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage:\n"
              << "  " << argv[0] << " call   <user> <pass> <callee> [host] [port] [duration_sec]\n"
              << "  " << argv[0] << " answer <user> <pass> [host] [port] [duration_sec]\n";
    return 1;
  }

  std::string mode = argv[1];

  if (mode == "call") {
    if (argc < 5) {
      std::cerr << "call requires: <user> <pass> <callee> [host] [port] [duration_sec]\n";
      return 1;
    }
    std::string user   = argv[2];
    std::string pass   = argv[3];
    std::string callee = argv[4];
    std::string host   = (argc > 5) ? argv[5] : "127.0.0.1";
    uint16_t port      = (argc > 6) ? static_cast<uint16_t>(std::stoi(argv[6])) : 9000;
    int duration       = (argc > 7) ? std::stoi(argv[7]) : 5;
    return do_call(user, pass, callee, host, port, duration);
  }

  if (mode == "answer") {
    if (argc < 4) {
      std::cerr << "answer requires: <user> <pass> [host] [port] [duration_sec]\n";
      return 1;
    }
    std::string user = argv[2];
    std::string pass = argv[3];
    std::string host = (argc > 4) ? argv[4] : "127.0.0.1";
    uint16_t port    = (argc > 5) ? static_cast<uint16_t>(std::stoi(argv[5])) : 9000;
    int duration     = (argc > 6) ? std::stoi(argv[6]) : 5;
    return do_answer(user, pass, host, port, duration);
  }

  std::cerr << "Unknown mode: " << mode << "\n";
  return 1;
}
