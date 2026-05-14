#!/usr/bin/env python3
"""
CardKB2 → Linux keyboard driver
Reads serial output from a CardKB2 and emits real keypresses via pynput.

Usage:
    python3 cardkb2_keyboard.py [--port /dev/ttyUSB0] [--baud 115200] [--debug]

Dependencies:
    pip install pyserial pynput
"""

import re
import sys
import argparse
import threading
import serial
from pynput.keyboard import Key, KeyCode, Controller

# ── Key-repeat settings ──────────────────────────────────────────────────────
# Indices of keys that should auto-repeat while held.
REPEAT_KEYS = {21}          # backspace / del (add more indices here if needed)
REPEAT_DELAY    = 0.40      # seconds before repeat starts (like a real keyboard)
REPEAT_INTERVAL = 0.05      # seconds between repeated presses (~20 cps)

# ── Serial output format ────────────────────────────────────────────────────
# Each event looks like:  KEY[ 0] '1' PRESS   or   KEY[21] BS RELEASE
LOG_RE = re.compile(r"KEY\[\s*(\d+)\]\s+(.+?)\s+(PRESS|RELEASE)")

# ── Key index → pynput action ───────────────────────────────────────────────
# Modifier / special keys that are tracked as held-down state
MODIFIER_KEYS = {22, 33, 34}   # Aa (caps/shift), Fn, Sym

# Map key index to what to press when NO modifier is active.
# Values can be:
#   str      → type that character
#   Key.*    → press that pynput special key
#   None     → handled by modifier logic / ignored
BASE_MAP: dict[int, str | Key | None] = {
    # Number row
    0:  '1',  1:  '2',  2:  '3',  3:  '4',  4:  '5',
    5:  '6',  6:  '7',  7:  '8',  8:  '9',  9:  '0',
    # QWERTY row
    11: 'q', 12: 'w', 13: 'e', 14: 'r', 15: 't',
    16: 'y', 17: 'u', 18: 'i', 19: 'o', 20: 'p',
    # Backspace
    21: Key.backspace,
    # Caps / shift toggle (Aa) — handled as modifier, but also mapped for clarity
    22: None,
    # Home row
    23: 'a', 24: 's', 25: 'd', 26: 'f', 27: 'g',
    28: 'h', 29: 'j', 30: 'k', 31: 'l',
    # Enter
    32: Key.enter,
    # Boot/Ctrl key (key index 44 in logs = '?', likely the boot/ctrl button)
    44: Key.ctrl_l,
    # Fn / Sym — modifiers, no direct output
    33: None,  # Fn
    34: None,  # Sym
    # Bottom row
    35: 'z', 36: 'x', 37: 'c', 38: 'v',
    39: 'b', 40: 'n', 41: 'm',
    # Space
    42: Key.space,
}

# Aa (caps/shift) active: uppercase + shifted number row characters
AA_MAP: dict[int, str | Key] = {
    # Number row → shifted symbols printed on board
    0: '!',  1: '@',  2: '#',  3: '$',  4: '%',
    5: '^',  6: '&',  7: '*',  8: '(',  9: ')',
    # Letters → uppercase
    11: 'Q', 12: 'W', 13: 'E', 14: 'R', 15: 'T',
    16: 'Y', 17: 'U', 18: 'I', 19: 'O', 20: 'P',
    21: Key.backspace,
    23: 'A', 24: 'S', 25: 'D', 26: 'F', 27: 'G',
    28: 'H', 29: 'J', 30: 'K', 31: 'L',
    32: Key.enter,
    35: 'Z', 36: 'X', 37: 'C', 38: 'V',
    39: 'B', 40: 'N', 41: 'M',
    42: Key.space,
}

# Sym active: symbols printed on board (second legend on each key)
# Entries that are 2-tuples mean: press shift + that key explicitly.
SYM_MAP: dict[int, str | Key | tuple] = {
    # Top row from board: Q=~ W=` E=? R=\ T=/ Y=| U=_ I=- O=+ P==
    11: '~', 12: '`', 13: '?', 14: '\\', 15: '/',
    16: (Key.shift, KeyCode.from_char('\\')),   # Y → Shift+\ = |
    17: '_', 18: '-', 19: '+', 20: '=',
    21: Key.delete,
    # Home row: A={ S=} D=[ F=[ G=] H=" J=' K=; L=:
    23: '{', 24: '}', 25: '[', 26: '[', 27: ']',
    28: '"', 29: "'", 30: ';', 31: ':',
    32: Key.enter,
    # Bottom row: Z=leftarrow X=downarrow C=rightarrow V=< B=> N=, M=.
    35: '<', 36: '>', 37: '<', 38: '<', 39: '>', 40: ',', 41: '.',
    42: Key.space,
}

# Fn active: arrow keys and special functions
FN_MAP: dict[int, str | Key] = {
    0:  Key.esc,        # fn+1 = ESC (printed on board)
    21: Key.tab,        # fn+del = TAB
    25: Key.up,         # fn+D = up arrow
    35: Key.left,       # fn+Z = left arrow
    36: Key.down,       # fn+X = down arrow
    37: Key.right,      # fn+C = right arrow
    # fn+Sym or fn+Aa could be added here
}


# ── Driver ───────────────────────────────────────────────────────────────────

class CardKB2Driver:
    def __init__(self, port: str, baud: int, debug: bool = False):
        self.port = port
        self.baud = baud
        self.debug = debug
        self.kb = Controller()

        # Modifier state
        self.fn_held  = False
        self.sym_held = False
        self.aa_held  = False   # Aa = Shift, held while pressed

        # Key-repeat state
        self._repeat_timer: threading.Timer | None = None
        self._repeat_lock = threading.Lock()

    def _stop_repeat(self):
        """Cancel any in-flight repeat timer."""
        with self._repeat_lock:
            if self._repeat_timer is not None:
                self._repeat_timer.cancel()
                self._repeat_timer = None

    def _fire_repeat(self, value: str | Key):
        """Press value once, then schedule the next repeat tick."""
        self.kb.press(value)
        self.kb.release(value)
        with self._repeat_lock:
            self._repeat_timer = threading.Timer(
                REPEAT_INTERVAL, self._fire_repeat, args=(value,)
            )
            self._repeat_timer.daemon = True
            self._repeat_timer.start()

    def _start_repeat(self, value: str | Key):
        """Fire the key once immediately, then begin the repeat chain after REPEAT_DELAY."""
        self._stop_repeat()
        # Immediate press
        self.kb.press(value)
        self.kb.release(value)
        # Schedule first repeat after initial delay
        with self._repeat_lock:
            self._repeat_timer = threading.Timer(
                REPEAT_DELAY, self._fire_repeat, args=(value,)
            )
            self._repeat_timer.daemon = True
            self._repeat_timer.start()

    def _dbg(self, *args):
        if self.debug:
            print("[DBG]", *args, file=sys.stderr)

    def _resolve(self, index: int) -> str | Key | None:
        """Return the pynput value for a key index given current modifier state."""
        if self.fn_held and index in FN_MAP:
            return FN_MAP[index]
        if self.sym_held and index in SYM_MAP:
            return SYM_MAP[index]
        # aa_held (Shift) is held at the OS level via kb.press(Key.shift_l),
        # so we just return the base key and let the OS apply the shift.
        return BASE_MAP.get(index)

    def _handle_event(self, index: int, raw_label: str, is_press: bool):
        self._dbg(f"idx={index} label={raw_label!r} {'PRESS' if is_press else 'RELEASE'}")

        # ── Modifier key events ──────────────────────────────────────────────
        if index == 22:          # Aa — real Shift (held while pressed)
            self.aa_held = is_press
            if is_press:
                self.kb.press(Key.shift_l)
            else:
                self.kb.release(Key.shift_l)
            self._dbg(f"  Aa/Shift → {self.aa_held}")
            return

        if index == 33:          # Fn — hold while pressed
            self.fn_held = is_press
            self._dbg(f"  Fn → {self.fn_held}")
            return

        if index == 34:          # Sym — hold while pressed
            self.sym_held = is_press
            self._dbg(f"  Sym → {self.sym_held}")
            return

        if index == 44:          # Boot/Ctrl
            if is_press:
                self.kb.press(Key.ctrl_l)
            else:
                self.kb.release(Key.ctrl_l)
            return

        # ── Regular key events (only act on PRESS for type; hold/release for specials) ─
        if not is_press:
            # Cancel repeat if a repeatable key is released (but not if fn+del=tab was used)
            if index in REPEAT_KEYS and not self.fn_held:
                self._stop_repeat()
            return  # CardKB2 doesn't send repeat; release is informational only

        value = self._resolve(index)
        if value is None:
            self._dbg(f"  No mapping for index {index}")
            return

        if index in REPEAT_KEYS and not self.fn_held:
            self._dbg(f"  → repeat_start({value!r})")
            self._start_repeat(value)
        elif isinstance(value, tuple):
            # Explicit (modifier, key) — press mod, tap key, release mod
            mod, k = value
            self._dbg(f"  → press({mod}+{k!r})")
            self.kb.press(mod)
            self.kb.press(k)
            self.kb.release(k)
            self.kb.release(mod)
        elif isinstance(value, str):
            if self.sym_held:
                # Sym layer symbols must be typed literally — kb.press() on shifted
                # characters (e.g. '|', '~') can produce wrong output when pynput
                # tries to resolve them against the current modifier state.
                self._dbg(f"  → type({value!r}) [sym literal]")
                self.kb.type(value)
            else:
                self._dbg(f"  → press({value!r})")
                self.kb.press(value)
                self.kb.release(value)
        elif isinstance(value, Key):
            self._dbg(f"  → press({value})")
            self.kb.press(value)
            self.kb.release(value)

    def run(self):
        print(f"Opening {self.port} @ {self.baud} baud …", file=sys.stderr)
        try:
            ser = serial.Serial(self.port, self.baud, timeout=0.1)
        except serial.SerialException as e:
            print(f"ERROR: {e}", file=sys.stderr)
            sys.exit(1)

        print("CardKB2 driver running. Press Ctrl+C to quit.", file=sys.stderr)
        buf = ""
        try:
            while True:
                chunk = ser.read(256).decode("ascii", errors="replace")
                if not chunk:
                    continue
                buf += chunk
                # Process every complete token (events are space-separated tokens)
                while True:
                    m = LOG_RE.search(buf)
                    if not m:
                        break
                    index     = int(m.group(1))
                    raw_label = m.group(2).strip()
                    action    = m.group(3)
                    is_press  = action == "PRESS"
                    self._handle_event(index, raw_label, is_press)
                    buf = buf[m.end():]  # consume matched portion
        except KeyboardInterrupt:
            print("\nExiting.", file=sys.stderr)
        finally:
            self._stop_repeat()
            ser.close()


# ── CLI ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="CardKB2 serial → Linux keyboard driver"
    )
    parser.add_argument(
        "--port", default="/dev/ttyUSB0",
        help="Serial port (default: /dev/ttyUSB0)"
    )
    parser.add_argument(
        "--baud", type=int, default=115200,
        help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--debug", action="store_true",
        help="Print key events to stderr"
    )
    args = parser.parse_args()

    driver = CardKB2Driver(port=args.port, baud=args.baud, debug=args.debug)
    driver.run()


if __name__ == "__main__":
    main()
