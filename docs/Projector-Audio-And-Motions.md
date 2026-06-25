# Projector, Audio, and Motion Notes

These notes are based on the original firmware command classes and motion
interpreter. The CLI aliases here still build database-backed MCU packets.

## Quick commands

```sh
./smb projector on
./smb projector off
./smb projector query
./smb speaker on
./smb speaker off
./smb list-all-commands PeripheralControl
./smb describe-command ProjectorCommand
./smb describe-command SpeakerCommand
```

`list-all-commands PeripheralControl` lists every command in the database whose
category is `PeripheralControl`. It is not a command by itself. Use it to find
things like projector, speaker, LED, and white-light commands.

## Projector

The simple projector power packet is:

```sh
./smb send-command ProjectorCommand switchMode=on
./smb send-command ProjectorCommand switchMode=off
```

The `projector` shortcut sends the same database command:

```sh
./smb projector on
./smb projector off
./smb projector query
```

The database also exposes lower-level projector settings:

```sh
./smb describe-command ProjectorTiXingSetting
./smb describe-command ProjectorPictureSetting
./smb describe-command ProjectorExpertMode
./smb describe-command ProjectorTypeSetting
./smb describe-command ProjectorOutputSetting
./smb describe-command ProjectorImageQualitySetting
```

The original firmware stores projection mode strings named `ohp_mode_wall` and
`ohp_mode_cell` under the key `ohp_mode`. The typo `cell` appears to mean
ceiling. Those settings are Android-side configuration for the projector app;
the MCU projector packets are the byte commands listed above.

## Projector head angle

The projector app has separate wall/ceiling projection state, and the firmware
also has ordinary head movement commands. The useful head commands in the MCU
database are:

```sh
./smb describe-command head
./smb send-command head mode=locate-absolute lock=no-lock horizontal=85 vertical=20
./smb send-command head mode=locate-absolute lock=no-lock hAngle=85 vAngle=10
./smb send-command head mode=absolute direction=vertical speed=3 angle=15
./smb send-command head mode=absolute direction=horizontal speed=3 angle=90
```

The firmware's combined-motion interpreter maps head JSON like
`mode=absoluteangle`, `hDegree`, and `vDegree` into absolute head-angle commands.
For wall/ceiling projection, use `ProjectorCommand` to turn the projector on,
then use `head` commands to position the projector angle. The exact automatic
wall/ceiling preset is app-side behavior, not a standalone MCU command named
"wall projection" or "ceiling projection" in the database.

## Speaker and audio

`SpeakerCommand` is a simple MCU switch:

```sh
./smb speaker on
./smb speaker off
./smb send-command SpeakerCommand switchMode=on
```

That does not stream arbitrary audio over the MCU USB protocol. In the original
firmware, text-to-speech and audio playback are Android-side audio APIs such as
`RTTSPlayer.speak(...)`, with volume/speed/pitch configured in the speech stack.
So this bridge can toggle the speaker MCU state, but outputting real audio needs
an audio path outside these MCU command packets.

## Named motions versus face expressions

`face 1..21` sends `LiliNormalExpression`, which is the MCU face-expression
packet:

```sh
./smb face 1
./smb send-command LiliNormalExpression expression_type=face-1
```

The names `smile`, `cry`, `angry`, and similar are not entries in
`assets/lily/expression.xml`; that file only maps face expressions by number.
Those names are combined-motion JSON files under `assets/motion/` in the
original firmware:

```text
abuse, angry, arrogance, cry, faint, goodbye, grievance, kiss, laughter,
picknose, prise, question, shy, sleep, smile, snicker, surprise, sweat
```

The combined-motion interpreter loads those JSON files and dispatches each step
to ordinary MCU movement commands:

- wheel `mode=distance` maps to distance wheel motion
- wheel `mode=angle` maps to relative wheel turn motion
- head `mode=absoluteangle` maps to absolute head-angle motion
- head `mode=relativeangle` maps to relative head-angle motion
- hand `mode=noangle` maps to up/down/stop/reset hand motion
- hand `mode=absoluteangle` maps to absolute hand-angle motion

That means a named motion is a choreography of head, hand, and wheel commands,
not one database command. The current CLI keeps these as raw movement commands
instead of embedding private firmware motion assets.
