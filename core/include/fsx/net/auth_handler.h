#pragma once

#include "fsx/db/user_repository.h"
#include "fsx/db/session_repository.h"
#include "fsx/auth/password_hash.h"
#include "fsx/protocol/auth_messages.h"
#include <string>

namespace fsx::net {

class AuthHandler {
 public:
  AuthHandler(fsx::db::UserRepository& users, fsx::db::SessionRepository& sessions)
    : users_(users), sessions_(sessions) {}

  fsx::protocol::RegisterResp handle_register(const fsx::protocol::RegisterReq& req);
  fsx::protocol::LoginResp handle_login(const fsx::protocol::LoginReq& req);

 private:
  fsx::db::UserRepository& users_;
  fsx::db::SessionRepository& sessions_;
};

} // namespace fsx::net

