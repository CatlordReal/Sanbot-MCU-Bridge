# Lights and face modes

This note records the firmware-backed light and face commands exposed by the
CLI. The command bytes still come from
`mcu-command-database/sanbot_mcu_commands.sqlite`; these names are convenience
wrappers and aliases around the same database-backed commands.

## White torch

The white torch has two command paths in the original firmware:

- `WhiteLightCommand`: payload bytes `0x04 0x01 switchMode`, routed to the head.
- `SetWhiteBrightness`: payload bytes `0x04 0x01 0x02 brightness`, routed to the
  head.

`WhiteLightCommand switchMode=on` only sends the on/off switch byte. The
firmware path that restores the torch after saved settings or physical-button
state also sends `SetWhiteBrightness` with action byte `0x02` and a nonzero
brightness. Observed saved brightness levels are `1`, `3`, and `5`.

Recommended commands:

```sh
./smb torch on
./smb torch off
./smb torch restore 5
./smb send-command SetWhiteBrightness brightness=5
./smb send-command QueryWhiteBrightness
```

`torch on` sends both `WhiteLightCommand switch=on` and
`SetWhiteBrightness setWhiteBrightness=restore brightness=5` by default.
`torch restore 5` sends only the brightness restore command. This is the command
to try after the physical torch button has turned the white light off and a
plain switch-on packet does not bring it back.

`SetWhiteBrightness` now defaults `setWhiteBrightness` to `0x02`, so
`brightness=5` is enough. `QueryWhiteBrightness` now defaults
`queryWhiteBrightness` to `0x03`.

Firmware evidence:

- `com/qihan/mcumanager/MCUManager.smali`: `WhiteLedSet(I)` builds
  `WhiteLightCommand` and sends command type `0x04`.
- `com/qihan/uvccamera/bean/WhiteLightCommand.smali`: message bytes are
  `0x04 0x01 switchMode`.
- `com/qihan/uvccamera/bean/SetWhiteBrightness.smali`: constructor sets action
  byte `0x02`; message bytes are `0x04 0x01 0x02 brightness`.
- `com/sunbo/main/MainService.smali`: saved LED grades restore brightness as
  `1`, `3`, or `5`.

## Normal LEDs

The normal LED command is `LEDLightCommand`:

```sh
./smb led all on
./smb led head on
./smb led left-arm blue
./smb led right-arm flicker-red 2 0
./smb led left-ear purple
./smb led right-ear yellow
./smb led head 0x18 2 0
./smb led 2 0x04 1 1
./smb send-command LEDLightCommand whichLight=head switchMode=on led_rate=0 led_random_number=0
./smb send-command LEDLightCommand whichLight=left-hand switchMode=blue led_rate=0 led_random_number=0
```

Payload shape is `0x04 0x02 whichLight switchMode led_rate led_random_number`.
The firmware uses the selected light to choose the route:

| CLI target | Byte | Route | Notes |
| --- | ---: | --- | --- |
| `all`, `broadcast` | `0x00` | both | Firmware point tag `0x03`. |
| `wheel`, `led1` / `1` | `0x01` | bottom | `LED.PART_WHEEL`. |
| `left-hand`, `left-arm`, `led2` / `2` | `0x02` | bottom | `LED.PART_LEFT_HAND`; physically an arm/hand LED. |
| `right-hand`, `right-arm`, `led3` / `3` | `0x03` | bottom | `LED.PART_RIGHT_HAND`; physically an arm/hand LED. |
| `left-head`, `left-ear`, `head-left`, `led4` / `4` | `0x04` | head | `LED.PART_LEFT_HEAD`; likely the left head/ear RGB LED. |
| `right-head`, `right-ear`, `head-right`, `led5` / `5` | `0x05` | head | `LED.PART_RIGHT_HEAD`; likely the right head/ear RGB LED. |
| `head`, `head-all`, `led10` / `10` | `0x0A` | head | Firmware transmits selector `0x00` but routes it to the head. |

RGB/color modes from `com/sunbo/main/bean/LED`:

| Mode alias | Byte |
| --- | ---: |
| `close` | `0x01` |
| `white` | `0x02` |
| `red` | `0x03` |
| `green` | `0x04` |
| `pink` | `0x05` |
| `purple` | `0x06` |
| `blue` | `0x07` |
| `yellow` | `0x08` |
| `flicker-white` | `0x12` |
| `flicker-red` | `0x13` |
| `flicker-green` | `0x14` |
| `flicker-pink` | `0x15` |
| `flicker-purple` | `0x16` |
| `flicker-blue` | `0x17` |
| `flicker-yellow` | `0x18` |
| `flicker-random` | `0x19` |
| `flicker-random-three-group` | `0x20` |

Head LED modes observed in `LedManager`:

| Mode name | Byte | Typical command |
| --- | ---: | --- |
| breathing end / off | `0x00` | `./smb led head off` |
| breathing start / on | `0x01` | `./smb led head on` |
| wakeup | `0x04` | `./smb led head 4` |
| mute start | `0x07` | `./smb led head 7` |
| video start | `0x03` | `./smb led head 3` |
| sleep / mute end | `0x0A` | `./smb led head 10` |
| video end | `0x14` | `./smb led head 20` |
| human control end | `0x1E` | `./smb led head 30` |
| human control start | `0x1F` | `./smb led head 31` |
| animated breathing pattern | `0x18` | `./smb led head 0x18 2 0` |

Firmware evidence:

- `com/qihan/mcumanager/MCUManager.smali`: `LedSet(IIII)` builds
  `LEDLightCommand`.
- `com/qihan/uvccamera/bean/LEDLightCommand.smali`: defines payload and route
  selection.
- `com/sunbo/main/bean/LED.smali`: defines physical LED part constants and
  RGB/color mode constants.
- `com/qihan/mcumanager/LedManager.smali`: defines head LED mode
  constants and mode-to-command mappings.

## Face modes

The normal face command is `LiliNormalExpression`:

```sh
./smb face 1
./smb face 20
./smb send-command LiliNormalExpression expression_type=face-1
```

Payload shape is `0x06 0x01 expression_type`, routed to the head. The firmware
exposes numeric face modes `1` through `21`. `assets/lily/expression.xml` uses
only numeric `name` attributes, so the CLI keeps these numeric names. The
separate `assets/motion/*.json` files have emotion-like names such as `smile`,
`cry`, and `surprise`, but those are body/head/wheel motion choreographies, not
an authoritative mapping to the 21 face IDs.

| Face | Asset path | Frame timing from `CN.CFG` |
| ---: | --- | --- |
| 1 | `assets/lily/1` | `NUMB:2#TIME:20,2` |
| 2 | `assets/lily/2` | `NUMB:2#TIME:2,2` |
| 3 | `assets/lily/3` | `NUMB:2#TIME:5,5` |
| 4 | `assets/lily/4` | `NUMB:6#TIME:5,1,1,5,1,1` |
| 5 | `assets/lily/5` | `NUMB:2#TIME:5,2` |
| 6 | `assets/lily/6` | `NUMB:3#TIME:5,2,5` |
| 7 | `assets/lily/7` | `NUMB:2#TIME:5,2` |
| 8 | `assets/lily/8` | `NUMB:2#TIME:2,2` |
| 9 | `assets/lily/9` | `NUMB:2#TIME:5,5` |
| 10 | `assets/lily/10` | `NUMB:4#TIME:2,2,2,2` |
| 11 | `assets/lily/11` | `NUMB:2#TIME:5,5` |
| 12 | `assets/lily/12` | `NUMB:2#TIME:10,10` |
| 13 | `assets/lily/13` | `NUMB:4#TIME:2,2,2,2` |
| 14 | `assets/lily/14` | `NUMB:4#TIME:10,1,5,1` |
| 15 | `assets/lily/15` | `NUMB:3#TIME:10,5,2` |
| 16 | `assets/lily/16` | `NUMB:6#TIME:5,2,2,2,2,5` |
| 17 | `assets/lily/17` | `NUMB:2#TIME:4,4` |
| 18 | `assets/lily/18` | `NUMB:2#TIME:5,5` |
| 19 | `assets/lily/19` | `NUMB:2#TIME:2,2` |
| 20 | `assets/lily/20` | `NUMB:9#TIME:2,2,2,2,2,2,2,3,5` |
| 21 | `assets/lily/21` | `NUMB:2#TIME:3,3` |

Firmware evidence:

- `com/qihan/uvccamera/bean/LiliNormalExpression.smali`: defines the
  `0x06 0x01 expression_type` payload.
- `assets/lily/expression.xml`: lists expression IDs `1` through `21`.
- `com/sunbo/main/MainService.smali`: sends `sub_cmd + 1` for normal
  expression commands and has a direct special send for expression `0x14`.
