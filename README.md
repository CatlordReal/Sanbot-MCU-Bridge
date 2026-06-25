# sanbot-mcu-bridge

A C++ library and CLI to control the Sanbot Elf S1-B2 humanoid robot over USB, bypassing the original Android controller.

## Quick start

First, install the CLI. Then, you can list, describe, and test.

```sh
cd core
./install-cli.sh
./smb commands
./smb list-all-commands
./smb list-all-commands PeripheralControl
./smb describe-command wheel
./smb --test --debug send-command wheel mode=distance direction=forward speed=50 distance=1000
```

Commands come from `mcu-command-database/sanbot_mcu_commands.sqlite`. Use
`commands` to list them and `describe-command NAME` to see the accepted fields.
Use `list-all-commands` to list categories, then pass a category name to see
each command and its description.

To start sending commands, you must take control of the USB:

```sh
./smb take-control
```

To listen for incoming packets, you can start the listener:

```sh
./smb listen
./smb listen 30
```

It runs until terminated, unless you pass a timeout.

### Command examples

Locomotion:

```sh
./smb send-command wheel mode=distance direction=forward speed=50 distance=1000
./smb send-command wheel mode=relative direction=left speed=40 angle=90
./smb send-command wheel mode=timed direction=turn-left time=1000 degree=90
./smb send-command wheel mode=no-angle direction=right-translation speed=40 time=1000 isCircle=0
./smb send-command wheel mode=no-angle direction=stop speed=0 time=0 isCircle=0
```

Lights:

```sh
./smb torch on
./smb torch off
./smb torch restore 5
./smb led all on
./smb led left-arm blue
./smb led right-arm flicker-red 2 0
./smb led left-ear purple
./smb led head 0x18 2 0
./smb send-command LEDLightCommand whichLight=1 switchMode=on led_rate=5 led_random_number=0
./smb send-command LEDLightCommand whichLight=left-hand switchMode=blue led_rate=0 led_random_number=0
./smb send-command WhiteLightCommand switchMode=on
./smb send-command SetWhiteBrightness setWhiteBrightness=restore brightness=5
./smb send-command SetWhiteBrightness brightness=5
./smb send-command QueryWhiteBrightness
```

See `docs/Lights-And-Face-Modes.md` for notes on how to use the original torch toggle (WhiteLightCommand).

Face modes:

```sh
./smb face 1
./smb face 20
./smb send-command LiliNormalExpression expression_type=face-1
```

Projector and speaker:

```sh
./smb projector on
./smb projector off
./smb projector query
./smb speaker on
./smb speaker off
./smb send-command ProjectorCommand switchMode=on
./smb send-command SpeakerCommand switchMode=on
```

See `docs/Projector-Audio-And-Motions.md` for projector notes.
Battery:

```sh
./smb send-command QueryBatteryCommand battery=0 currentBattery=0
./smb send-command BatteryTemperatureCommand temperature=0
./smb send-command AutoBatteryCommand switchMode=on threshold=20
./smb send-command AutoBatteryCommand switchMode=off
```

The same examples are available from the binary:

```sh
./smb help
./smb examples
```

## The project

**This project is currently in development - it's not ready yet!**

It aims to create a comprehensive and easy-to-use CLI and library to control the Sanbot Elf S1-B2 from (almost) any device, fully bypassing the original Android board. This will be used in a project of mine called Sunny-Sanbot, which you can find on my GitHub profile.

## Roadmap

- [x] Working packet send/receive to MCUs
- [x] Database-backed command catalogue exposed to the CLI/library
- [ ] Hardware-test most database-backed commands
- [ ] Audio & Camera bridge
- [ ] C++ library

## Notes from the dev

The sources for algorithms, addresses and commands are private, since I can't publish them as they're not open source (I pulled it from the firmware such that it's legal for me to reference where necessary but not use or share).

Some number of docs were partially or fully written by ChatGPT Codex. In the majority of cases, I wrote vague notes on what I did, and it compiled it into something that other people could read.

This library had a Python predecessor. For more info, see docs/History.md.

## License

This repository is currently unlicensed.
You may view the code, but you do not have permission to use, modify, or redistribute it outside GitHub.
