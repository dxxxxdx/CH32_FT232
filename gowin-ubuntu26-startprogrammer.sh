#!/usr/bin/env bash
set -Eeuo pipefail
shopt -s nullglob

unset LD_LIBRARY_PATH LD_PRELOAD QT_QPA_PLATFORM QT_QPA_PLATFORM_PLUGIN_PATH

BASE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
ROOT="$BASE/Programmer/bin"
USB_SYS="/sys/bus/usb/devices"
CLAIM_SHIM_DIR=""
CLAIM_SHIM=""

if [[ ! -x "$ROOT/programmer" ]]; then
    echo "找不到：$ROOT/programmer" >&2
    exit 1
fi

FREETYPE="$(ldconfig -p 2>/dev/null | awk '$1=="libfreetype.so.6" {print $NF; exit}')"
ZLIB="$(ldconfig -p 2>/dev/null | awk '$1=="libz.so.1" {print $NF; exit}')"

if [[ -z "$FREETYPE" || -z "$ZLIB" ]]; then
    echo "找不到系统 libfreetype.so.6 或 libz.so.1" >&2
    exit 1
fi

PLUGIN_DIR="$(
    find "$ROOT" -type f -name libqxcb.so -printf '%h\n' -quit 2>/dev/null || true
)"

# 0403:6010 是双通道 FT2232H，0403:6014 是单通道 FT232H。
declare -a DEVICES=()

for dev in "$USB_SYS"/*; do
    [[ -r "$dev/idVendor" && -r "$dev/idProduct" ]] || continue

    vid="$(tr '[:upper:]' '[:lower:]' < "$dev/idVendor")"
    pid="$(tr '[:upper:]' '[:lower:]' < "$dev/idProduct")"

    case "$vid:$pid" in
        0403:6010|0403:6014)
            DEVICES+=("${dev##*/}")
            ;;
    esac
done

if (( ${#DEVICES[@]} == 0 )); then
    echo "未找到兼容 FTDI 下载器（0403:6010 或 0403:6014）。" >&2
    exit 1
fi

TARGET="${GOWIN_USB_PATH:-}"

if [[ -n "$TARGET" ]]; then
    found=0
    for d in "${DEVICES[@]}"; do
        [[ "$d" == "$TARGET" ]] && found=1
    done

    (( found )) || {
        echo "GOWIN_USB_PATH=$TARGET 不是支持的 FTDI 下载器。" >&2
        echo "支持的 VID:PID：0403:6010、0403:6014" >&2
        exit 1
    }
elif (( ${#DEVICES[@]} == 1 )); then
    TARGET="${DEVICES[0]}"
else
    echo "发现多个兼容 FTDI 设备：" >&2
    printf '  %s\n' "${DEVICES[@]}" >&2
    echo "例如：GOWIN_USB_PATH=1-5.2.2 ./sp.sh" >&2
    exit 1
fi

if [[ "${1:-}" == "--cli" ]]; then
    PROGRAM="$ROOT/programmer_cli"
    shift
else
    PROGRAM="$ROOT/programmer"
fi

if [[ ! -x "$PROGRAM" ]]; then
    echo "找不到：$PROGRAM" >&2
    exit 1
fi

BUS_RAW="$(<"$USB_SYS/$TARGET/busnum")"
DEV_RAW="$(<"$USB_SYS/$TARGET/devnum")"
printf -v USB_BUS '%03u' "$((10#$BUS_RAW))"
printf -v USB_DEV '%03u' "$((10#$DEV_RAW))"
USB_DEVNODE="/dev/bus/usb/$USB_BUS/$USB_DEV"

if [[ ! -c "$USB_DEVNODE" ]]; then
    echo "找不到下载器设备节点：$USB_DEVNODE" >&2
    exit 1
fi

LOG_DIR="${GOWIN_LOG_DIR:-$BASE/logs}"
RUN_STAMP="$(date '+%Y%m%d-%H%M%S')"
TRACE_LOG="$LOG_DIR/gowin-usb-$RUN_STAMP.log"
APP_LOG="$ROOT/$(date '+%Y-%m-%d').log"
APP_LOG_START=0
APP_LOG_CAPTURED=0
LIBUSB_DEBUG_LEVEL="${GOWIN_LIBUSB_DEBUG:-1}"
CLAIM_TRACE="${GOWIN_USB_CLAIM_TRACE:-1}"
URB_TRACE="${GOWIN_USB_URB_TRACE:-0}"

case "$LIBUSB_DEBUG_LEVEL" in
    0|1|2|3|4) ;;
    *)
        echo "GOWIN_LIBUSB_DEBUG 只能是 0..4，当前值：$LIBUSB_DEBUG_LEVEL" >&2
        exit 1
        ;;
esac

case "$CLAIM_TRACE:$URB_TRACE" in
    [01]:[01]) ;;
    *)
        echo "GOWIN_USB_CLAIM_TRACE 和 GOWIN_USB_URB_TRACE 只能是 0 或 1。" >&2
        exit 1
        ;;
esac

mkdir -p -- "$LOG_DIR"
if [[ -f "$APP_LOG" ]]; then
    APP_LOG_START="$(stat -c '%s' "$APP_LOG")"
fi

# 正常运行只记录 libusb 错误；完整 URB 跟踪由环境变量显式打开。
exec > >(tee -a "$TRACE_LOG") 2>&1

declare -a UNBOUND_IFACES=()
MM_WAS_ACTIVE=0

append_application_log() {
    local current_size

    if (( APP_LOG_CAPTURED != 0 )); then
        return 0
    fi
    APP_LOG_CAPTURED=1
    if [[ ! -f "$APP_LOG" ]]; then
        return 0
    fi

    current_size="$(stat -c '%s' "$APP_LOG")"
    if (( current_size > APP_LOG_START )); then
        echo "----- Gowin application log -----"
        tail -c "+$((APP_LOG_START + 1))" "$APP_LOG"
        echo "----- end application log -----"
    fi
    return 0
}

cleanup() {
    rc=$?
    trap - EXIT INT TERM
    set +e

    for (( i=${#UNBOUND_IFACES[@]}-1; i>=0; --i )); do
        iface="${UNBOUND_IFACES[i]}"

        if [[ -e "$USB_SYS/$iface" && -e /sys/bus/usb/drivers/ftdi_sio/bind ]]; then
            echo "恢复 ftdi_sio：$iface"
            printf '%s' "$iface" | sudo tee /sys/bus/usb/drivers/ftdi_sio/bind >/dev/null
        fi
    done

    if (( MM_WAS_ACTIVE )); then
        echo "恢复 ModemManager"
        sudo systemctl start ModemManager >/dev/null 2>&1
    fi

    if [[ -n "$CLAIM_SHIM_DIR" && -d "$CLAIM_SHIM_DIR" ]]; then
        rm -rf -- "$CLAIM_SHIM_DIR"
    fi

    append_application_log
    echo "完整诊断日志：$TRACE_LOG"
    exit "$rc"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

build_claim_shim() {
    local source_file

    command -v cc >/dev/null 2>&1 || {
        echo "找不到 C 编译器，无法生成 Gowin libusb claim 补丁。" >&2
        return 1
    }

    CLAIM_SHIM_DIR="$(mktemp -d --tmpdir gowin-usb-claim.XXXXXX)"
    source_file="$CLAIM_SHIM_DIR/claim.c"
    CLAIM_SHIM="$CLAIM_SHIM_DIR/libgowin-usb-claim.so"

    cat >"$source_file" <<'CLAIM_SOURCE'
#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <linux/usbdevice_fs.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef int (*ioctl_fn_t)(int fd, unsigned long request, ...);

#define USB_TRACE_EVENT_LIMIT (1024U)
#define USB_TRACE_BYTE_LIMIT  (32U)

static ioctl_fn_t real_ioctl_fn;
static pthread_mutex_t owner_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
static int owner_fd = -1;
static uint32_t trace_event_count;

static ioctl_fn_t resolve_ioctl(void)
{
    void *symbol;

    if (real_ioctl_fn != NULL) {
        return real_ioctl_fn;
    }

    symbol = dlsym(RTLD_NEXT, "ioctl");
    if (symbol != NULL) {
        memcpy(&real_ioctl_fn, &symbol, sizeof(real_ioctl_fn));
    }
    return real_ioctl_fn;
}

static int is_target_usb_fd(int fd)
{
    const char *target;
    char fd_path[64];
    char actual_path[PATH_MAX];
    ssize_t actual_len;
    int path_len;

    target = getenv("GOWIN_USB_DEVNODE");
    if ((target == NULL) || (target[0] == '\0')) {
        return 0;
    }

    path_len = snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
    if ((path_len <= 0) || ((size_t)path_len >= sizeof(fd_path))) {
        return 0;
    }

    actual_len = readlink(fd_path, actual_path, sizeof(actual_path) - 1U);
    if (actual_len <= 0) {
        return 0;
    }
    actual_path[(size_t)actual_len] = '\0';
    return strcmp(actual_path, target) == 0;
}

static uint8_t trace_enabled(void)
{
    const char *trace;

    trace = getenv("GOWIN_USB_CLAIM_TRACE");
    return (uint8_t)((trace != NULL) && (trace[0] == '1'));
}

static uint8_t urb_trace_enabled(void)
{
    const char *trace;

    trace = getenv("GOWIN_USB_URB_TRACE");
    return (uint8_t)((trace != NULL) && (trace[0] == '1'));
}

static void trace_urb(const char *phase, int fd,
                      const struct usbdevfs_urb *urb,
                      int ioctl_result, int ioctl_errno)
{
    const uint8_t *bytes;
    uint32_t byte_count;
    uint32_t index;
    uint8_t dump_payload;
    char line[512];
    size_t used;
    ssize_t write_result;
    int written;

    if ((urb_trace_enabled() == 0U) || (urb == NULL)) {
        return;
    }

    pthread_mutex_lock(&trace_lock);
    if (trace_event_count >= USB_TRACE_EVENT_LIMIT) {
        if (trace_event_count == USB_TRACE_EVENT_LIMIT) {
            static const char limit_message[] =
                "[gowin-usb] URB trace limit reached\n";
            write_result = write(STDERR_FILENO, limit_message,
                                 sizeof(limit_message) - 1U);
            (void)write_result;
            trace_event_count++;
        }
        pthread_mutex_unlock(&trace_lock);
        return;
    }
    trace_event_count++;

    written = snprintf(line, sizeof(line),
                       "[gowin-usb] %-8s fd=%d type=%u ep=0x%02x "
                       "buffer=%d actual=%d status=%d ioctl=%d errno=%d",
                       phase,
                       fd,
                       (unsigned int)urb->type,
                       (unsigned int)urb->endpoint,
                       urb->buffer_length,
                       urb->actual_length,
                       urb->status,
                       ioctl_result,
                       ioctl_errno);
    used = (written > 0) ? (size_t)written : 0U;
    if (used >= sizeof(line)) {
        used = sizeof(line) - 1U;
    }

    dump_payload = (uint8_t)(
        ((urb->type == USBDEVFS_URB_TYPE_CONTROL) &&
         (strcmp(phase, "submit") == 0)) ||
        (((urb->endpoint & 0x80U) == 0U) &&
         (strcmp(phase, "submit") == 0)) ||
        (((urb->endpoint & 0x80U) != 0U) &&
         (strcmp(phase, "complete") == 0)));

    bytes = (const uint8_t *)urb->buffer;
    if ((dump_payload != 0U) && (bytes != NULL)) {
        byte_count = (strcmp(phase, "complete") == 0)
                         ? (uint32_t)((urb->actual_length > 0)
                                          ? urb->actual_length : 0)
                         : (uint32_t)((urb->buffer_length > 0)
                                          ? urb->buffer_length : 0);
        if (byte_count > USB_TRACE_BYTE_LIMIT) {
            byte_count = USB_TRACE_BYTE_LIMIT;
        }
        for (index = 0U;
             (index < byte_count) && (used < (sizeof(line) - 4U));
             index++) {
            written = snprintf(&line[used], sizeof(line) - used,
                               " %02x", (unsigned int)bytes[index]);
            if (written <= 0) {
                break;
            }
            used += (size_t)written;
        }
    }
    if (used >= (sizeof(line) - 1U)) {
        used = sizeof(line) - 2U;
    }
    line[used++] = '\n';
    write_result = write(STDERR_FILENO, line, used);
    (void)write_result;
    pthread_mutex_unlock(&trace_lock);
}

static int forward_claim(ioctl_fn_t real_ioctl, int fd, unsigned long argument)
{
    const unsigned int *interface_number;
    int claim_result;

    interface_number = (const unsigned int *)(uintptr_t)argument;
    pthread_mutex_lock(&owner_lock);
    claim_result = real_ioctl(fd, USBDEVFS_CLAIMINTERFACE, argument);
    if ((claim_result == 0) && (interface_number != NULL) &&
        (*interface_number == 0U)) {
        owner_fd = fd;
        if (trace_enabled() != 0U) {
            dprintf(STDERR_FILENO,
                    "[gowin-usb] interface 0 owner fd=%d (application claim)\n",
                    fd);
        }
    }
    pthread_mutex_unlock(&owner_lock);
    return claim_result;
}

static int forward_release(ioctl_fn_t real_ioctl, int fd,
                           unsigned long argument)
{
    const unsigned int *interface_number;
    int release_result;

    interface_number = (const unsigned int *)(uintptr_t)argument;
    pthread_mutex_lock(&owner_lock);
    release_result = real_ioctl(fd, USBDEVFS_RELEASEINTERFACE, argument);
    if ((release_result == 0) && (interface_number != NULL) &&
        (*interface_number == 0U) && (owner_fd == fd)) {
        owner_fd = -1;
        if (trace_enabled() != 0U) {
            dprintf(STDERR_FILENO,
                    "[gowin-usb] interface 0 released fd=%d\n",
                    fd);
        }
    }
    pthread_mutex_unlock(&owner_lock);
    return release_result;
}

static void move_jtag_owner(ioctl_fn_t real_ioctl, int fd)
{
    unsigned int interface_number = 0U;
    int previous_owner;
    int release_result = 0;
    int claim_result;
    int claim_errno;
    int previous_errno;

    previous_errno = errno;
    pthread_mutex_lock(&owner_lock);
    if (owner_fd == fd) {
        pthread_mutex_unlock(&owner_lock);
        return;
    }

    previous_owner = owner_fd;
    errno = 0;
    claim_result = real_ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interface_number);
    claim_errno = errno;

    if ((claim_result != 0) && (claim_errno == EBUSY) &&
        (previous_owner >= 0) && (previous_owner != fd) &&
        is_target_usb_fd(previous_owner)) {
        release_result = real_ioctl(previous_owner,
                                    USBDEVFS_RELEASEINTERFACE,
                                    &interface_number);
        if (release_result == 0) {
            owner_fd = -1;
            errno = 0;
            claim_result = real_ioctl(fd,
                                      USBDEVFS_CLAIMINTERFACE,
                                      &interface_number);
            claim_errno = errno;
        }
    }

    if (claim_result == 0) {
        owner_fd = fd;
    }

    if (trace_enabled() != 0U) {
        dprintf(STDERR_FILENO,
                "[gowin-usb] handoff interface 0: %d -> %d, "
                "release=%d claim=%d errno=%d\n",
                previous_owner,
                fd,
                release_result,
                claim_result,
                claim_errno);
    }
    pthread_mutex_unlock(&owner_lock);
    errno = previous_errno;
}

int ioctl(int fd, unsigned long request, ...)
{
    ioctl_fn_t real_ioctl;
    struct usbdevfs_urb *urb;
    struct usbdevfs_urb **reaped_urb;
    unsigned long argument;
    int ioctl_result;
    int ioctl_errno;
    int target_fd;
    va_list arguments;

    va_start(arguments, request);
    argument = va_arg(arguments, unsigned long);
    va_end(arguments);

    real_ioctl = resolve_ioctl();
    if (real_ioctl == NULL) {
        errno = ENOSYS;
        return -1;
    }

    target_fd = is_target_usb_fd(fd);
    if (target_fd != 0) {
        if (request == USBDEVFS_CLAIMINTERFACE) {
            return forward_claim(real_ioctl, fd, argument);
        }
        if (request == USBDEVFS_RELEASEINTERFACE) {
            return forward_release(real_ioctl, fd, argument);
        }
        if (request == USBDEVFS_SUBMITURB) {
            urb = (struct usbdevfs_urb *)(uintptr_t)argument;
            move_jtag_owner(real_ioctl, fd);
            errno = 0;
            ioctl_result = real_ioctl(fd, request, argument);
            ioctl_errno = errno;
            trace_urb("submit", fd, urb, ioctl_result, ioctl_errno);
            errno = ioctl_errno;
            return ioctl_result;
        }
        if ((request == USBDEVFS_REAPURB) ||
            (request == USBDEVFS_REAPURBNDELAY)) {
            errno = 0;
            ioctl_result = real_ioctl(fd, request, argument);
            ioctl_errno = errno;
            if ((ioctl_result == 0) && (argument != 0UL)) {
                reaped_urb = (struct usbdevfs_urb **)(uintptr_t)argument;
                trace_urb("complete", fd, *reaped_urb,
                          ioctl_result, ioctl_errno);
            }
            errno = ioctl_errno;
            return ioctl_result;
        }
        if ((request == USBDEVFS_BULK) ||
            (request == USBDEVFS_RESETEP) ||
            (request == USBDEVFS_CLEAR_HALT) ||
            (request == USBDEVFS_SETINTERFACE)) {
            move_jtag_owner(real_ioctl, fd);
        }
    }

    return real_ioctl(fd, request, argument);
}
CLAIM_SOURCE

    cc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared \
        "$source_file" -pthread -ldl -o "$CLAIM_SHIM"
}

# Gowin 扫描和烧录使用了不同句柄，却把 interface 0 留在扫描句柄上。
# 补丁只在当前进程、当前 USB 节点内把接口所有权交给真正发请求的句柄。
build_claim_shim

echo "===== Gowin USB diagnostic run ====="
date --iso-8601=seconds
uname -a
echo "程序：$PROGRAM"
echo "目标下载器：$TARGET  ($USB_DEVNODE)"
echo "VID:PID：$(<"$USB_SYS/$TARGET/idVendor"):$(<"$USB_SYS/$TARGET/idProduct")"
echo "USB 速度：$(<"$USB_SYS/$TARGET/speed") Mbit/s"
echo "内置 libusb：$ROOT/libusb-1.0.so"
echo "系统 libusb：$(pkg-config --modversion libusb-1.0 2>/dev/null || echo unknown)"
echo "本次使用 libusb：高云内置版本"
echo "LIBUSB_DEBUG：$LIBUSB_DEBUG_LEVEL"
echo "USBFS URB 十六进制跟踪：$URB_TRACE"
echo "诊断日志：$TRACE_LOG"
sudo -v

if command -v systemctl >/dev/null 2>&1 &&
   systemctl is-active --quiet ModemManager; then
    echo "停止 ModemManager"
    sudo systemctl stop ModemManager
    MM_WAS_ACTIVE=1
fi

# A 通道（interface 0）归 JTAG；B 通道保留给 ftdi_sio/UART。
for iface_dir in "$USB_SYS/$TARGET:1.0"; do
    [[ -d "$iface_dir" ]] || continue
    [[ -L "$iface_dir/driver" ]] || continue

    iface="${iface_dir##*/}"
    driver="$(basename "$(readlink -f "$iface_dir/driver")")"

    [[ "$driver" == "ftdi_sio" ]] || continue
    [[ -e /sys/bus/usb/drivers/ftdi_sio/unbind ]] || continue

    echo "解绑 ftdi_sio：$iface"
    printf '%s' "$iface" | sudo tee /sys/bus/usb/drivers/ftdi_sio/unbind >/dev/null
    UNBOUND_IFACES+=("$iface")
done

echo "启动：$(basename "$PROGRAM")"
set +e

if [[ -n "$PLUGIN_DIR" ]]; then
    env \
        GOWIN_USB_DEVNODE="$USB_DEVNODE" \
        GOWIN_USB_CLAIM_TRACE="$CLAIM_TRACE" \
        GOWIN_USB_URB_TRACE="$URB_TRACE" \
        LIBUSB_DEBUG="$LIBUSB_DEBUG_LEVEL" \
        LD_LIBRARY_PATH="$ROOT" \
        LD_PRELOAD="$CLAIM_SHIM:$ZLIB:$FREETYPE" \
        QT_QPA_PLATFORM=xcb \
        QT_QPA_PLATFORM_PLUGIN_PATH="$PLUGIN_DIR" \
        "$PROGRAM" "$@"
else
    env \
        GOWIN_USB_DEVNODE="$USB_DEVNODE" \
        GOWIN_USB_CLAIM_TRACE="$CLAIM_TRACE" \
        GOWIN_USB_URB_TRACE="$URB_TRACE" \
        LIBUSB_DEBUG="$LIBUSB_DEBUG_LEVEL" \
        LD_LIBRARY_PATH="$ROOT" \
        LD_PRELOAD="$CLAIM_SHIM:$ZLIB:$FREETYPE" \
        QT_QPA_PLATFORM=xcb \
        "$PROGRAM" "$@"
fi

RC=$?
set -e
echo "Gowin Programmer 退出码：$RC"
append_application_log
exit "$RC"
