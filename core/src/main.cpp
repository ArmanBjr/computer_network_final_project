#include "fsx/db/db.h"
#include "fsx/db/user_repository.h"
#include "fsx/db/session_repository.h"
#include "fsx/net/tcp_server.h"
#include "fsx/net/auth_handler.h"
#include "fsx/net/session_manager.h"
#include "fsx/net/udp_voice_server.h"
#include "fsx/transfer/transfer_manager.h"
#include "fsx/storage/file_store.h"
#include "fsx/storage/resume_store.h"
#include "fsx/crypto/rsa.h"
#include "fsx/voice/voice_manager.h"
#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>

// Make stdout unbuffered for Docker logs
static struct UnbufferedStdout {
  UnbufferedStdout() {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
  }
} unbuffered_stdout;

static std::string env_or(const char* k, const char* defv) {
  const char* v = std::getenv(k);
  return v ? std::string(v) : std::string(defv);
}

static int env_int_or(const char* k, int defv) {
  const char* v = std::getenv(k);
  if (!v || strlen(v) == 0) return defv;
  try {
    return std::stoi(v);
  } catch (...) {
    return defv;
  }
}

int main(int argc, char** argv) {
  try {
    // Connect to database
    fsx::db::DbConfig cfg;
    cfg.host = env_or("FSX_DB_HOST", "localhost");
    cfg.port = env_int_or("FSX_DB_PORT", 5432);
    cfg.user = env_or("FSX_DB_USER", "fsx");
    cfg.password = env_or("FSX_DB_PASSWORD", "fsxpass");
    cfg.name = env_or("FSX_DB_NAME", "fsx");

    fsx::db::Db db(cfg);
    db.connect();
    std::cout << "[DB] connected\n";
    std::cout.flush();

    // Create repositories
    fsx::db::UserRepository users(db);
    fsx::db::SessionRepository sessions(db);

    // Create auth handler
    fsx::net::AuthHandler auth_handler(users, sessions);

    // Create session manager
    fsx::net::SessionManager session_manager;

    // Create transfer manager and file store (Phase 3)
    fsx::transfer::TransferManager transfer_manager;
    fsx::storage::FileStore file_store("./storage/transfers");
    if (!file_store.initialize()) {
      std::cerr << "fatal: failed to initialize file store\n";
      return 1;
    }
    std::cout << "[storage] initialized\n";
    std::cout.flush();
    
    // Phase 5: Create resume store and ensure transfer_resume table exists
    fsx::storage::ResumeStore resume_store(db);
    resume_store.ensure_table();
    std::cout << "[resume] store initialized\n";
    std::cout.flush();

    // Phase 8: RSA key pair for key exchange (server sends pubkey, client sends encrypted session key)
    fsx::crypto::RsaKeyPair rsa_keypair;
    rsa_keypair.generate();
    std::cout << "[core] RSA key pair generated (key exchange)\n";
    std::cout.flush();

    // Start TCP server
    uint16_t port = 9000;
    if (argc >= 2) {
      try {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
      } catch (...) {
        std::cerr << "Warning: invalid port argument, using default 9000\n";
      }
    }
    // Also check environment variable
    int env_port = env_int_or("FSX_TCP_PORT", 0);
    if (env_port > 0 && env_port <= 65535) {
      port = static_cast<uint16_t>(env_port);
    }

    // Phase 10: Voice manager and UDP voice port
    auto voice_manager = std::make_shared<fsx::voice::VoiceManager>();
    uint16_t udp_port = static_cast<uint16_t>(env_int_or("FSX_UDP_PORT", 9001));

    boost::asio::io_context io;

    // TCP server (passes voice_manager to each TcpSession for signaling)
    fsx::net::TcpServer server(io, port, auth_handler, session_manager, transfer_manager,
                               file_store, resume_store, users, rsa_keypair,
                               voice_manager, udp_port);
    server.start();

    // Phase 10: UDP voice relay server
    fsx::net::UdpVoiceServer udp_voice(io, udp_port, voice_manager);
    udp_voice.start();
    std::cout << "[core] voice UDP relay on port " << udp_port << "\n";
    std::cout.flush();

    std::cout << "[core] server started on port " << port << ", running...\n";
    std::cout.flush();
    io.run();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
