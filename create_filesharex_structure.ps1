# create_filesharex_structure.ps1
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\create_filesharex_structure.ps1

$root = "E:\University\University_Subjects\5th\Computer_Networks\Projects\finl_project\filesharex"

# Helper: ensure directory exists
function Ensure-Dir($path) {
    if (-not (Test-Path -LiteralPath $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

# Helper: ensure empty file exists (and parent dir)
function Ensure-File($path) {
    $parent = Split-Path -Parent $path
    Ensure-Dir $parent
    if (-not (Test-Path -LiteralPath $path)) {
        New-Item -ItemType File -Path $path | Out-Null
    }
}

# Create root
Ensure-Dir $root

# ---- Top-level files ----
Ensure-File (Join-Path $root "README.md")

# ---- docker ----
Ensure-Dir (Join-Path $root "docker")
Ensure-File (Join-Path $root "docker\compose.yml")
Ensure-File (Join-Path $root "docker\core.Dockerfile")
Ensure-File (Join-Path $root "docker\gateway.Dockerfile")
Ensure-Dir  (Join-Path $root "docker\postgres")
Ensure-File (Join-Path $root "docker\postgres\init.sql")

# ---- docs ----
Ensure-Dir (Join-Path $root "docs")
Ensure-Dir (Join-Path $root "docs\protocol")
Ensure-File (Join-Path $root "docs\protocol\fsx_app_protocol.md")
Ensure-File (Join-Path $root "docs\protocol\admin_api_protocol.md")
Ensure-File (Join-Path $root "docs\protocol\voice_protocol.md")

Ensure-Dir (Join-Path $root "docs\wireshark")
Ensure-File (Join-Path $root "docs\wireshark\filters.md")
Ensure-Dir  (Join-Path $root "docs\wireshark\screenshots")
# چهار مورد اجباری (placeholder)
Ensure-File (Join-Path $root "docs\wireshark\screenshots\01_placeholder.txt")
Ensure-File (Join-Path $root "docs\wireshark\screenshots\02_placeholder.txt")
Ensure-File (Join-Path $root "docs\wireshark\screenshots\03_placeholder.txt")
Ensure-File (Join-Path $root "docs\wireshark\screenshots\04_placeholder.txt")
Ensure-File (Join-Path $root "docs\wireshark\demo_scenarios.md")

Ensure-Dir (Join-Path $root "docs\architecture")
Ensure-File (Join-Path $root "docs\architecture\components.md")
Ensure-File (Join-Path $root "docs\architecture\state_machines.md")
Ensure-File (Join-Path $root "docs\architecture\threat_model.md")

Ensure-File (Join-Path $root "docs\report.log")

# ---- core (C++ server) ----
Ensure-Dir  (Join-Path $root "core")
Ensure-File (Join-Path $root "core\CMakeLists.txt")

# include/fsx tree
$coreInclude = Join-Path $root "core\include\fsx"
Ensure-Dir $coreInclude

Ensure-Dir (Join-Path $coreInclude "common")
Ensure-File (Join-Path $coreInclude "common\types.h")
Ensure-File (Join-Path $coreInclude "common\time.h")
Ensure-File (Join-Path $coreInclude "common\result.h")
Ensure-File (Join-Path $coreInclude "common\config.h")

Ensure-Dir (Join-Path $coreInclude "net")
Ensure-File (Join-Path $coreInclude "net\tcp_server.h")
Ensure-File (Join-Path $coreInclude "net\tcp_session.h")
Ensure-File (Join-Path $coreInclude "net\framed_reader.h")
Ensure-File (Join-Path $coreInclude "net\framed_writer.h")
Ensure-File (Join-Path $coreInclude "net\udp_socket.h")

Ensure-Dir (Join-Path $coreInclude "protocol")
Ensure-File (Join-Path $coreInclude "protocol\message.h")
Ensure-File (Join-Path $coreInclude "protocol\codec.h")
Ensure-File (Join-Path $coreInclude "protocol\app_messages.h")
Ensure-File (Join-Path $coreInclude "protocol\constants.h")

Ensure-Dir (Join-Path $coreInclude "auth")
Ensure-File (Join-Path $coreInclude "auth\auth_service.h")
Ensure-File (Join-Path $coreInclude "auth\password_hash.h")

Ensure-Dir (Join-Path $coreInclude "storage")
Ensure-File (Join-Path $coreInclude "storage\transfer_store.h")
Ensure-File (Join-Path $coreInclude "storage\file_store.h")
Ensure-File (Join-Path $coreInclude "storage\resume_store.h")

Ensure-Dir (Join-Path $coreInclude "transfer")
Ensure-File (Join-Path $coreInclude "transfer\transfer_manager.h")
Ensure-File (Join-Path $coreInclude "transfer\chunker.h")
Ensure-File (Join-Path $coreInclude "transfer\retransmit.h")
Ensure-File (Join-Path $coreInclude "transfer\throttler.h")
Ensure-File (Join-Path $coreInclude "transfer\integrity.h")

Ensure-Dir (Join-Path $coreInclude "crypto")
Ensure-File (Join-Path $coreInclude "crypto\key_exchange.h")
Ensure-File (Join-Path $coreInclude "crypto\aes_gcm.h")
Ensure-File (Join-Path $coreInclude "crypto\rsa.h")
Ensure-File (Join-Path $coreInclude "crypto\sha256.h")

Ensure-Dir (Join-Path $coreInclude "compress")
Ensure-File (Join-Path $coreInclude "compress\zlib_codec.h")

Ensure-Dir (Join-Path $coreInclude "voice")
Ensure-File (Join-Path $coreInclude "voice\voice_manager.h")
Ensure-File (Join-Path $coreInclude "voice\opus_codec.h")
Ensure-File (Join-Path $coreInclude "voice\jitter_buffer.h")

Ensure-Dir (Join-Path $coreInclude "admin")
Ensure-File (Join-Path $coreInclude "admin\admin_server.h")
Ensure-File (Join-Path $coreInclude "admin\admin_messages.h")
Ensure-File (Join-Path $coreInclude "admin\metrics.h")

Ensure-Dir (Join-Path $coreInclude "log")
Ensure-File (Join-Path $coreInclude "log\logger.h")
Ensure-File (Join-Path $coreInclude "log\audit_log.h")

# core/src tree
$coreSrc = Join-Path $root "core\src"
Ensure-Dir $coreSrc
Ensure-File (Join-Path $coreSrc "main.cpp")
Ensure-Dir  (Join-Path $coreSrc "net")
Ensure-Dir  (Join-Path $coreSrc "protocol")
Ensure-Dir  (Join-Path $coreSrc "auth")
Ensure-Dir  (Join-Path $coreSrc "storage")
Ensure-Dir  (Join-Path $coreSrc "transfer")
Ensure-Dir  (Join-Path $coreSrc "crypto")
Ensure-Dir  (Join-Path $coreSrc "compress")
Ensure-Dir  (Join-Path $coreSrc "voice")
Ensure-Dir  (Join-Path $coreSrc "admin")
Ensure-Dir  (Join-Path $coreSrc "log")

# ---- client (C++ client) ----
Ensure-Dir  (Join-Path $root "client")
Ensure-File (Join-Path $root "client\CMakeLists.txt")

$clientInc = Join-Path $root "client\include\fsx_client"
Ensure-Dir $clientInc
Ensure-File (Join-Path $clientInc "cli.h")
Ensure-File (Join-Path $clientInc "client_app.h")
Ensure-File (Join-Path $clientInc "session.h")
Ensure-File (Join-Path $clientInc "file_sender.h")
Ensure-File (Join-Path $clientInc "file_receiver.h")
Ensure-File (Join-Path $clientInc "voice_client.h")
Ensure-File (Join-Path $clientInc "progress_ui.h")
Ensure-File (Join-Path $clientInc "config.h")

Ensure-Dir  (Join-Path $root "client\src")
Ensure-File (Join-Path $root "client\src\main.cpp")
# placeholder for "..."
Ensure-File (Join-Path $root "client\src\_placeholder.txt")

# ---- gateway (FastAPI) ----
Ensure-Dir  (Join-Path $root "gateway")
Ensure-File (Join-Path $root "gateway\pyproject.toml")

$gwApp = Join-Path $root "gateway\app"
Ensure-Dir  $gwApp
Ensure-File (Join-Path $gwApp "main.py")
Ensure-File (Join-Path $gwApp "settings.py")
Ensure-File (Join-Path $gwApp "core_client.py")

Ensure-Dir  (Join-Path $gwApp "api")
Ensure-File (Join-Path $gwApp "api\routes_admin.py")
Ensure-File (Join-Path $gwApp "api\ws_dashboard.py")

Ensure-Dir  (Join-Path $gwApp "services")
Ensure-File (Join-Path $gwApp "services\dashboard_state.py")
Ensure-File (Join-Path $gwApp "services\auth_admin.py")

Ensure-Dir  (Join-Path $gwApp "templates")
Ensure-File (Join-Path $gwApp "templates\layout.html")
Ensure-File (Join-Path $gwApp "templates\dashboard.html")

Ensure-Dir  (Join-Path $gwApp "static\css")
Ensure-Dir  (Join-Path $gwApp "static\js")

Ensure-Dir  (Join-Path $root "gateway\tests")

# ---- scripts ----
Ensure-Dir  (Join-Path $root "scripts")
Ensure-File (Join-Path $root "scripts\run_dev_wsl.sh")
Ensure-File (Join-Path $root "scripts\run_demo.sh")
Ensure-File (Join-Path $root "scripts\gen_test_files.py")

# ---- .github/workflows ----
Ensure-Dir  (Join-Path $root ".github\workflows")
Ensure-File (Join-Path $root ".github\workflows\ci.yml")

Write-Host "Done. FileShareX structure created at: $root"
