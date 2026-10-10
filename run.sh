#!/bin/bash
# =================================================================
# /H/aisrc/MUJI/run.sh — 零下载零安装版 (weston 范式)
# =================================================================
# 核心设计：
#   1. 运行时 — 所有系统 .so 在 squashfs 底层（reset 不丢）
#               libwordnet / libzim / libicu / libxapian → 解压在 toolchain-rootfs
#               libglfw / libGL / libstdc++ / libc 等 → squashfs 原生
#               dictd daemon → 解压 .deb 到 toolchain-rootfs + 持久化 .dbm
#   2. 编译时 — cmake + ninja + 头文件 + pkg-config
#               从 /H/usr/aisrc/apt/muji/*.deb 解压到 toolchain-rootfs
#
# 用法:
#   bash run.sh bootstrap    # 第一次: 解压 .deb → toolchain-rootfs (只跑一次)
#   bash run.sh build        # 用 toolchain-rootfs cmake 重编 (overlay reset 后也能编)
#   bash run.sh              # 启动 6-panel ImGui 工作台 (one-shot screenshot)
#   bash run.sh cli          # 启动 GUI + 后台 dictd daemon
#   bash run.sh native       # 只跑原生 CLI 验证
#   bash run.sh dictd        # 启动 dictd daemon (GoldenDict 数据源)
#   bash run.sh stop         # 停所有后台
#   bash run.sh status       # 查看全部状态
# =================================================================
set -e

BASE="/H/aisrc/MUJI"
BUILD="$BASE/build"
TOOLCHAIN="$BASE/toolchain-rootfs"
DEBS="/H/usr/aisrc/apt/muji"
LOG="/H/usr/aisrc/log"
ZIM_DIR="$BASE/dic/zim"
DICT_DIR="$BASE/dic/extracted"
DICTD_DIR="$BASE/dictd-data"          # dictd 持久化 .dbm
mkdir -p "$LOG" "$ZIM_DIR" "$DICTD_DIR"

log() { echo "[$(date +%H:%M:%S)] $*"; }
err() { echo "✗ $*" >&2; exit 1; }

# =================================================================
# fix_bin — Live reset 后必须修 /bin
# =================================================================
fix_bin() {
    if [ ! -L /bin/bash ] || [ ! -L /bin/sh ] || [ ! -L /bin/env ]; then
        log "修 /bin..."
        sudo mkdir -p /tmp/binwrapper
        for b in bash sh env ls cp mv rm mkdir sed grep cat; do
            sudo ln -sf "/usr/bin/$b" "/tmp/binwrapper/$b" 2>/dev/null || true
        done
        sudo mount --bind /tmp/binwrapper /bin
        sudo sh -c 'grep -q "/tmp/binwrapper /bin" /etc/fstab || echo "/tmp/binwrapper /bin none bind 0 0" >> /etc/fstab'
    fi
    /bin/bash -c 'true' || err "/bin/bash 坏了 — 手动: fix_bin.sh"
}

# =================================================================
# stop / status
# =================================================================
if [ "${1:-}" = "stop" ]; then
    pkill -f "build/muji" 2>/dev/null || true
    pkill -f "build/bin/dict_cli" 2>/dev/null || true
    pkill -f "build/bin/stardict_cli" 2>/dev/null || true
    pkill -f "build/bin/kiwix_cli" 2>/dev/null || true
    # dictd 如果用 toolchain 路径启动的
    pkill -f "dictd" 2>/dev/null || true
    sleep 1
    echo "✅ 已停止所有 MUJI 进程"
    exit 0
fi

if [ "${1:-}" = "status" ]; then
    echo "=== MUJI 环境状态 ==="
    echo "时间: $(date)"
    echo "DISPLAY: ${DISPLAY:-unset}"
    echo
    echo "=== 关键二进制 ==="
    for f in "$BUILD/muji" \
             "$BUILD/bin/jieba_cli" "$BUILD/bin/wn_cli" \
             "$BUILD/bin/dict_cli"  "$BUILD/bin/stardict_cli" \
             "$BUILD/bin/kiwix_cli"; do
        [ -f "$f" ] && echo "✅ $f ($(du -h "$f" | cut -f1))" || echo "❌ MISSING $f"
    done
    echo
    echo "=== toolchain-rootfs ==="
    if [ -d "$TOOLCHAIN/usr/bin" ]; then
        echo "✅ 存在 ($(du -sh "$TOOLCHAIN" | cut -f1))"
        for b in cmake ninja gcc; do
            p="$TOOLCHAIN/usr/bin/$b"
            [ -f "$p" ] && echo "  ✅ $b" || echo "  ❌ $b"
        done
        for so in libwordnet-3.0.so libzim.so.9 libkiwix.so.14 libxapian.so.30 libicuuc.so.76; do
            found=$(find "$TOOLCHAIN" -name "$so" 2>/dev/null | head -1)
            [ -n "$found" ] && echo "  ✅ $so" || echo "  ❌ $so"
        done
    else
        echo "❌ toolchain-rootfs 不存在 → 需要: bash $0 bootstrap"
    fi
    echo
    echo "=== .deb 源 ==="
    cnt=$(ls "$DEBS"/*.deb 2>/dev/null | wc -l)
    [ "$cnt" -gt 0 ] && echo "✅ $cnt 个 .deb ($(du -sh "$DEBS" 2>/dev/null | cut -f1))" || echo "❌ 无 .deb"
    echo
    echo "=== dictd ==="
    pgrep -f "dictd" >/dev/null && echo "✅ dictd 运行中" || echo "❌ dictd 未运行"
    echo
    echo "=== dicts ==="
    cnt=$(ls "$ZIM_DIR"/*.zim 2>/dev/null | wc -l)
    echo "ZIM: $cnt 个 (.zim)"
    cnt=$(find "$DICT_DIR" -name "*.ifo" 2>/dev/null | wc -l)
    echo "StarDict: $cnt 个 (.ifo)"
    exit 0
fi

# =================================================================
# bootstrap — 解压 .deb → toolchain-rootfs (只跑一次)
# =================================================================
bootstrap() {
    echo "=============================================="
    echo "  MUJI Bootstrap — 解压 .deb → toolchain-rootfs"
    echo "=============================================="
    fix_bin

    echo 'DPkg::Pre-Install-Pkgs {};' | sudo tee /etc/apt/apt.conf.d/20listchanges > /dev/null 2>/dev/null || true

    if [ ! -d "$DEBS" ] || [ "$(ls "$DEBS"/*.deb 2>/dev/null | wc -l)" -eq 0 ]; then
        err "没有 .deb 在 $DEBS — 先: 把 /var/cache/apt/archives/*.deb cp 过去"
    fi

    mkdir -p "$TOOLCHAIN"
    log "解压 $(ls "$DEBS"/*.deb 2>/dev/null | wc -l) 个 .deb → $TOOLCHAIN"
    for deb in "$DEBS"/*.deb; do
        [ -f "$deb" ] || continue
        dpkg-deb -x "$deb" "$TOOLCHAIN/" 2>/dev/null || true
    done
    log "✅ $(du -sh "$TOOLCHAIN" | cut -f1)"

    # dictd 需要 config + database (.dbm) — 从 /etc/dictd 也解压
    log "准备 dictd 持久化目录 → $DICTD_DIR"
    mkdir -p "$DICTD_DIR/var/lib/dictd/db"
    # 如果 overlay 上已有 dictd 数据库文件, 复制到 /H 持久化
    if [ -d /usr/share/dictd ]; then
        cp -r /usr/share/dictd/* "$DICTD_DIR/" 2>/dev/null || true
    fi
    log "Bootstrap 完成！"
    log "编译:  bash $0 build"
    log "运行:  bash $0"
    log "dictd: bash $0 dictd"
}

# =================================================================
# build — cmake 重编 (overlay reset 后也能编)
# =================================================================
run_build() {
    fix_bin

    if [ ! -d "$TOOLCHAIN/usr/bin" ]; then
        log "toolchain-rootfs 不存在 → 先 bootstrap"
        bootstrap
    fi

    log "=== cmake build ==="
    export PATH="$TOOLCHAIN/usr/bin:$PATH"
    export LD_LIBRARY_PATH="$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/usr/lib:$LD_LIBRARY_PATH"
    export PKG_CONFIG_PATH="$TOOLCHAIN/usr/lib/x86_64-linux-gnu/pkgconfig:$TOOLCHAIN/usr/share/pkgconfig:$PKG_CONFIG_PATH"

    log "which cmake=$(which cmake 2>/dev/null || echo NOT FOUND)"
    log "which gcc=$(which gcc 2>/dev/null || echo NOT FOUND)"
    log "which ninja=$(which ninja 2>/dev/null || echo NOT FOUND)"

    cd "$BASE"
    cmake -S . -B build 2>&1 | tail -3 > "$LOG/cmake_setup_$(date +%Y%m%d_%H%M%S).log"
    cmake --build build -j"$(nproc)" > "$LOG/cmake_build_$(date +%Y%m%d_%H%M%S).log" 2>&1 || {
        echo "❌ cmake build 失败！tail:"; tail -30 "$LOG/cmake_build_"*.log; exit 1
    }

    log "✅ 编译完成!"
}

# =================================================================
# dictd — 启动 GoldenDict 数据源 daemon
# =================================================================
run_dictd() {
    fix_bin

    if [ -d "$TOOLCHAIN/usr/sbin/dictd" ]; then
        DICTD_BIN="$TOOLCHAIN/usr/sbin/dictd"
    else
        DICTD_BIN=$(command -v dictd 2>/dev/null || echo "")
    fi
    [ -z "$DICTD_BIN" ] && err "dictd 找不到 — 先: bash $0 bootstrap"

    # dictd config (用 /H 上的 database 路径)
    DICTD_CONF="$DICTD_DIR/dictd.conf"
    cat > "$DICTD_CONF" <<EOF
# MUJI dictd config (持久化路径)
database gcide {
    data  "$DICTD_DIR/gcide/gcide"
    index "$DICTD_DIR/gcide/gcide.index"
}
database wn {
    data  "$DICTD_DIR/wn/wn"
    index "$DICTD_DIR/wn/wn.index"
}
access { allow * }
EOF
    log "启动 dictd: $DICTD_BIN"
    export LD_LIBRARY_PATH="$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/usr/lib"
    "$DICTD_BIN" --config "$DICTD_CONF" --port 2628 2>&1 &
    sleep 2
    # 验证
    if timeout 2 bash -c 'echo "SHOW DB" | nc 127.0.0.1 2628' 2>/dev/null | grep -q "^150"; then
        log "✅ dictd 运行中 (port 2628)"
    else
        log "⚠ dictd 启动失败 (可能数据库不在 $DICTD_DIR) — 不致命，dict_cli 面板会跳过"
    fi
}

# =================================================================
# 启动检查
# =================================================================
check_build() {
    [ -f "$BUILD/muji" ] || err "❌ build/muji 不存在 — 先: bash $0 build"
}

# =================================================================
# mcp — 启动 lan-agent serve + MUJI MCP bridge
# =================================================================
run_mcp() {
    fix_bin
    check_build

    # ── 1. lan-agent serve ──
    LA_ROOT="/H/aisrc/lan-agent"
    LA_BIN="$LA_ROOT/build/codex_lan_agent"
    LA_CFG="/tmp/lan_agent_mcu.cfg"
    LA_MACHINE_CODE="9C4F-29CD-0DCA-1749"

    if [ ! -f "$LA_BIN" ]; then
        log "lan-agent 没 build — 跳过 (MCP bridge 面板会显示 DISCONNECTED)"
    else
        cat > "$LA_CFG" <<EOF
listen_host=127.0.0.1
listen_port=18080
workspace_root=/H/aisrc/MUJI
log_root=/tmp/lan-agent-logs
task_timeout_sec=1800
EOF
        # kill 旧的
        pkill -f "codex_lan_agent" 2>/dev/null || true
        sleep 1

        log "启动 lan-agent serve (machine-code=$LA_MACHINE_CODE)"
        cd "$LA_ROOT"
        ./build/codex_lan_agent --config "$LA_CFG" serve \
            --machine-code "$LA_MACHINE_CODE" > "$LOG/lan-agent_$(date +%Y%m%d_%H%M%S).log" 2>&1 &
        sleep 4

        if ss -tlnp 2>/dev/null | grep -q :18080; then
            log "✅ lan-agent MCP on :18080"
        else
            log "⚠ lan-agent 没起来 — MUJI 仍会启，但 MCP 面板 DISCONNECTED"
        fi
    fi

    # ── 2. MUJI GUI with MCP bridge ──
    [ -z "${DISPLAY:-}" ] && err "无 DISPLAY — 需要 Xvnc/Xorg"

    export MUJI_MCP_URL="http://127.0.0.1:18080/mcp"
    OUT="/H/usr/aisrc/out/muji_mcp_$(date +%Y%m%d_%H%M%S).ppm"
    log "=== MUJI 7-panel (incl LanAgent MCP) ==="
    log "DISPLAY=$DISPLAY  MUJI_MCP_URL=$MUJI_MCP_URL  OUT=$OUT"

    "$BUILD/muji" > "$LOG/muji_mcp_$(date +%Y%m%d_%H%M%S).log" 2>&1
    rc=$?
    log "muji exit=$rc"
    if [ -f "$OUT" ]; then
        log "✅ 截图: $OUT ($(du -h "$OUT" | cut -f1))"
    fi
    exit $rc
}

# =================================================================
# 分发
# =================================================================
MODE="${1:-gui}"
case "$MODE" in
    bootstrap)  bootstrap; exit 0 ;;
    build)      run_build; exit 0 ;;
    dictd)      run_dictd; exit 0 ;;
    mcp)        run_mcp; exit 0 ;;
    stop)       : ;;
    status)     : ;;
esac

# ==================== 运行时 (gui / cli / native) ====================
fix_bin
check_build

# 设 LD_LIBRARY_PATH 指向 toolchain-rootfs (libwordnet/libzim/etc)
export LD_LIBRARY_PATH="$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/usr/lib:${LD_LIBRARY_PATH:-}"

case "$MODE" in
    native)
        # 只跑 CLI 验证所有层
        echo "=============================================="
        echo "  MUJI Native CLI Suite"
        echo "=============================================="
        log "L0 jieba_cli..."
        "$BUILD/bin/jieba_cli" -m ex "NUMA架构下，跨节点内存访问延迟高于本地内存" 2>&1 | head -3
        log "L1 wn_cli..."
        "$BUILD/bin/wn_cli" memory -n -s 2>&1 | head -4
        log "L2b stardict_cli..."
        timeout 10 "$BUILD/bin/stardict_cli" list -D "$DICT_DIR" 2>&1 | head -2
        log "✅ native CLI 全部可运行"
        ;;
    *)
        # GUI (one-shot screenshot)
        [ -z "${DISPLAY:-}" ] && err "无 DISPLAY — 需要 Xvnc/Xorg，或: bash $0 native"
        log "=== MUJI 6-panel ImGui workspace ==="
        log "DISPLAY=$DISPLAY  MUJI_SCREENSHOT=/H/usr/aisrc/out/muji_$(date +%H%M%S).ppm"

        OUT="/H/usr/aisrc/out/muji_$(date +%Y%m%d_%H%M%S).ppm"
        DISPLAY="${DISPLAY:-:10.0}" MUJI_SCREENSHOT="$OUT" "$BUILD/muji" > "$LOG/muji_$(date +%Y%m%d_%H%M%S).log" 2>&1
        rc=$?
        log "muji exit=$rc"
        if [ -f "$OUT" ]; then
            log "✅ 截图: $OUT ($(du -h "$OUT" | cut -f1))"
        fi
        exit $rc
        ;;
esac
