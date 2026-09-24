# Controls (LG webOS TV remote)

Number keys and the d-pad are mapped to Minecraft actions. Everything is
handled in `src/pc/webos/WebOSInput.cpp`.

## In game

| Remote button | Action |
|---|---|
| Arrow Up / Down / Left / Right | Move forward / back / strafe left / strafe right (W / S / A / D) |
| OK | Jump (Space) |
| 1 | Escape (pause menu / close screen) |
| 2 | Inventory (E) |
| 3 | Right click (place block / use item) |
| 4 | Left click (attack / break block, hold to mine) |
| 5 / 8 | Look up / down |
| 7 / 9 | Look left / right |
| 6 | Next hotbar slot |
| 0 | Open chat |
| Back | Escape |

## In menus and inventory

| Remote button | Action |
|---|---|
| Arrow keys | Move the on-screen cursor (speeds up while held) |
| OK | Left click |
| 3 | Right click |
| 4 | Left click |
| 1 | Escape |
| 2 | Inventory / close inventory (E) |
| Other digits | Typed as normal digits |

## Gamepad

Works with any controller SDL recognises (Xbox / PlayStation / most Bluetooth
and USB pads). Controllers SDL has no mapping for are treated as Xbox-layout.

| Gamepad | In the world | In menus / inventory / chat |
|---|---|---|
| Left stick | W / A / S / D | move the cursor (walks over the keys while the keyboard is up) |
| Right stick | look around | move the cursor |
| A | jump | left click (types the highlighted key while the keyboard is up) |
| B | Q (drop item) | Escape |
| X | - | right click |
| Y | inventory (E) | E (closes the inventory; ignored on text screens) |
| RT | break block (left click) | left click |
| LT | place / use (right click) | right click |
| RB / LB | next / previous hotbar slot | scroll |
| Start | Escape | Escape |
| D-pad up | F3 (debug screen) | move the cursor one inventory slot up (keyboard up: move up) |
| D-pad down | - | one slot down (text screens: show / hide the on-screen keyboard) |
| D-pad left / right | - | one slot left / right (keyboard up: move left / right) |
| Right stick click (R3), held | - | precision cursor (about 1/3 speed) |
| Left stick click (L3) | open chat (T) | closes the chat |

The log shows `[PAD] ...` lines when a controller is connected and for the first
button presses, which helps if a button is mapped wrongly.

## On-screen keyboard (text screens only)

Chat, the multiplayer address, world name / seed, renaming a world and sign
editing open a QWERTY keyboard automatically. It is never shown while you are
just playing in the world. On the Options screen (player name) click the name
field, then press **2** to open it.

| Remote button | Action while the keyboard is open |
|---|---|
| Arrow keys | Move between keys |
| OK | Press the highlighted key |
| 2 | Show / hide the keyboard |
| 1 | Escape |

Keys: `Aa` = shift (one letter), `<-` = backspace, `Enter` sends chat / confirms,
`Done` hides the keyboard so you can use the cursor on the buttons. There are
also `.`, `-`, `:`, `_`, `@` for server addresses.

## Cursor

The remote cursor (arrow keys / Magic Remote) is shown in menus and GUI screens
(inventory, chat, ...) and hidden while playing in the world.

## In-game settings (webOS)

- **Options > Video Settings > Render Scale**: Auto / 100% / 80% / 67% / 50%. Auto aims for 60 fps (MCBETA_TARGET_FPS).
- **Options > Controls > Gamepad Look / Gamepad Cursor**: sliders, 25% - 200% (100% = default). Saved in `options.txt`.

## Notes

- Digits 1-4 are remote buttons everywhere; the other digits only in game.
  Set `MCBETA_REMOTE_DIGITS=0` to give the digits back to a real keyboard.
- A Magic Remote pointer (via LGNC, when available) moves the cursor and looks
  around; its left/right buttons are left/right click.
- A mouse over VNC also works for looking and clicking.

## Environment variables

| Variable | Effect |
|---|---|
| `MCBETA_REMOTE_DIGITS=0` | Do not treat number keys as remote buttons |
| `MCBETA_FULLSCREEN=1` | Ask SDL for a fullscreen window (default: plain window) |
| `MCBETA_ALPHA=1` | Request an 8-bit alpha channel (default: none) |
| `MCBETA_SWAP_INTERVAL=0` | Turn vsync off |
| `MCBETA_WEBOS_LOG=/path` | Log file (default `/tmp/mcbetacpp-webos.log`) |
| `MCBETA_RENDER_SCALE=auto\|0.5..0.99\|off` | Force the render scale (normally set in Options > Video Settings > Render Scale; when set, the in-game value is ignored) |
| `MCBETA_TARGET_FPS=60` | Frame rate the automatic render scale aims for |
| `MCBETA_PAD_LOOK=1.0` / `MCBETA_PAD_CURSOR=1.0` | Extra gamepad camera / cursor speed multipliers on top of Options > Controls |
| `MCBETA_GL_CHECK=1` | Re-enable the per-frame glGetError checks (debugging) |

All of these can also be put in `mcbeta.env` next to the executable (KEY=VALUE per line), which is how to set them when the app is started from the launcher.
