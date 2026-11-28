#!/usr/bin/env bash
#
# start.sh — bring up the pixel-stealing demo in the right order:
#   1. virtual display (Xvnc)        -> so there is a screen to draw on
#   2. window manager (metacity)     -> so the Firefox window gets focus + can go full screen
#   3. attacker/victim HTTP servers  -> serve.py on ports 8000 (attacker) and 8001 (victim)
#   4. Firefox 27 pointed at the attacker page (launched in the foreground)
#
# Everything is idempotent: if a piece is already running it is left alone, so
# you can re-run this safely. Quit Firefox cleanly (File > Quit) so it remembers
# the maximized window size for next time.
#
# Override any default with an env var, e.g.:
#   DISPLAY_NUM=2 GEOMETRY=1280x1024 ./start.sh

set -u

# --- config (override via environment) --------------------------------------
DISPLAY_NUM="${DISPLAY_NUM:-1}"                       # Xvnc display number N -> DISPLAY=:N, VNC port 5900+N
GEOMETRY="${GEOMETRY:-1024x768}"
DEPTH="${DEPTH:-24}"
FIREFOX="${FIREFOX:-$HOME/firefox/firefox}"
PROFILE="${PROFILE:-$HOME/ff27profile}"
VNC_PASSWD="${VNC_PASSWD:-$HOME/.vnc/passwd}"
ATTACKER_URL="${ATTACKER_URL:-http://127.0.0.1:8000}"

DISPLAY_STR=":${DISPLAY_NUM}"
export DISPLAY="$DISPLAY_STR"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

say() { printf '>> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# Locate serve.py whether the folder is named subnormal_stealing/ or subnormal/
SERVE=""
for c in "$SCRIPT_DIR/subnormal_stealing/serve.py" "$SCRIPT_DIR/subnormal/serve.py" "$SCRIPT_DIR/serve.py"; do
  [ -f "$c" ] && SERVE="$c" && break
done
[ -n "$SERVE" ] || die "could not find serve.py near $SCRIPT_DIR"

# --- helpers ----------------------------------------------------------------
# True if a TCP port on localhost is accepting connections (no extra tools needed).
port_open() { (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && exec 3>&- ; }

# --- 1. virtual display -----------------------------------------------------
if [ -e "/tmp/.X11-unix/X${DISPLAY_NUM}" ]; then
  say "Xvnc already up on ${DISPLAY_STR} (VNC port $((5900 + DISPLAY_NUM)))"
else
  [ -f "$VNC_PASSWD" ] || die "no VNC password file at $VNC_PASSWD — run: vncpasswd $VNC_PASSWD"
  say "starting Xvnc on ${DISPLAY_STR} (${GEOMETRY}, depth ${DEPTH}, VNC port $((5900 + DISPLAY_NUM)))"
  Xvnc "$DISPLAY_STR" -geometry "$GEOMETRY" -depth "$DEPTH" \
       -SecurityTypes VncAuth -PasswordFile "$VNC_PASSWD" >/dev/null 2>&1 &
  for _ in $(seq 1 20); do
    [ -e "/tmp/.X11-unix/X${DISPLAY_NUM}" ] && break
    sleep 0.25
  done
  [ -e "/tmp/.X11-unix/X${DISPLAY_NUM}" ] || die "Xvnc did not come up on ${DISPLAY_STR}"
fi

# --- 2. window manager ------------------------------------------------------
# Needed so the Firefox window receives keyboard focus and can be maximized / F11'd.
if pgrep -u "$(id -u)" -x metacity >/dev/null 2>&1; then
  say "metacity already running"
else
  say "starting metacity window manager"
  metacity --display "$DISPLAY_STR" >/dev/null 2>&1 &
  sleep 1
fi

# --- 3. attacker + victim servers -------------------------------------------
if port_open 8000; then
  say "server already listening on :8000 / :8001 (leaving it running)"
else
  say "starting serve.py ($SERVE)"
  python3 "$SERVE" &
  for _ in $(seq 1 20); do
    port_open 8000 && break
    sleep 0.25
  done
  port_open 8000 || die "serve.py did not start listening on :8000"
fi

# --- 4. Firefox -------------------------------------------------------------
[ -x "$FIREFOX" ] || die "Firefox not found/executable at $FIREFOX (set FIREFOX=/path/to/firefox)"
[ -d "$PROFILE" ] || die "profile dir not found at $PROFILE (set PROFILE=/path/to/profile)"

say "launching Firefox at $ATTACKER_URL"
say "tip: maximize the window or press F11, then quit via File > Quit so the size sticks"
exec "$FIREFOX" -no-remote -profile "$PROFILE" "$ATTACKER_URL"
