Mcbetacpp 1.7.3 for webos 


its very cool and thats mine first homebrew for webos 

controls:

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
| Left stick click (L3) | debug menu

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


yeah i used claude and my brain but not that much of my brain 

todoh:

add a gamepad support done 
optimisate it more good to get 60 fps maybe done 
better controls done 
webos keyboard implementation done 
