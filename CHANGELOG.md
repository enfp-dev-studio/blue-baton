# Changelog

Notable changes in each release, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
(sections Added, Changed, Fixed, Removed; only the ones a release has), and versions follow
[Semantic Versioning](https://semver.org/). The release workflow copies a version's section into its GitHub release.

## [0.7.8] - 2026-10-18

### Fixed

- Pair Again with all 3 host slots in use said there was no slot; pairing again does not need one. The window now opens, and a new computer that tries to pair meanwhile is turned away with a message
- The host pairing window could not be closed after the board had restarted while it was open (Close and the X did nothing); the app now closes it when the board restarts or confirms the close
- The board could restart when a computer removed from it kept reconnecting with its old keys (seen with a Windows PC, about 15 times in 9 seconds). After 4 failed host connections within 10 seconds the board now stops advertising for 10 seconds and says so in the app
- Removing a host while the board was trying to reconnect an input device left the computer's address key in the Bluetooth controller; the board now pauses its radio for the removal, as it already did for input devices
- Removing a host now asks to also remove the board on that computer, by the board's name
- A computer that stopped reconnecting is shown as such, with what to do: macOS gives up on an accessory after 5 connections that end before its keyboard service is in use, and only Connect on that Mac brings it back (removing the host on the board would throw its keys away). Seen on a Mac whose link timed out repeatedly within 80 seconds
- The app keeps more of the board log (8 MB before it rotates, was 1 MB), so a problem from hours ago can still be traced

## [0.7.7] - 2026-10-17

### Changed

- Hosts: a computer that forgot the board pairs again into its own row with More → Pair Again (or Allow on the notice that it is trying to); before, this took Add on an empty row
- Each board has its own Bluetooth name: "Blue Baton" and the last four hex digits of its address (for example "Blue Baton 3F2A"), shown at the top of the app, so two boards can be told apart on a computer
- ESP32-C6 is not supported: with two computers connected at the same interval, the chip's Bluetooth controller lets one of the links time out every few minutes (README, Boards)
- An input device that asks for a connection interval lining up with the computers' links (7.5 or 15 ms, or a whole multiple of 7.5 ms) is refused and asks again; the ESP32-S3 controller already refused these itself

### Fixed

- Writing firmware no longer shows a "Board disconnected" error while the board restarts
- A development build of the app offered the firmware image it carried under the source tree's version, and could write an older firmware; the version now comes from the image itself

## [0.7.6] - 2026-10-16

### Changed

- The code typed on the board's keyboard to pair a computer shows as digits in the app, to check against the computer's screen (it showed only dots)

### Fixed

- ESP32-C6 builds: some input devices never connected (seen: a Xiaoxin Mplus 5.0 mouse, HCI 0x3E on every attempt); input connections now go through the controller's multi-connection scheduling

## [0.7.5] - 2026-10-16

### Changed

- The board tells the app which chip its firmware is built for; the app offers its firmware (ESP32-S3) only to ESP32-S3 boards, and installing it on another chip ends with a message that says so
- "Add device" lists only what is advertising during the current search, not devices found by earlier searches

### Fixed

- Adding a device stayed at "Connecting" when the link failed to be established; it now ends with "Couldn't connect"
- Firmware built for a single-core chip (ESP32-C3, C5, C6, H2) restarted over and over right after starting

## [0.7.4] - 2026-10-15

### Fixed

- Input devices that start the MTU exchange themselves (seen: AK87 SLIM PRO keyboard) were disconnected right after encryption and kept reconnecting
- A failure that repeats no longer stacks the same notice; notices have a close button and work while a dialog is open

## [0.7.3] - 2026-10-15

### Changed

- Default switch combos are Alt+1, 2, 3 (Option on a Mac keyboard), one hand on both Mac and PC keyboards. Combos you set yourself stay; "Restore default combos" in the app picks up the new ones

### Fixed

- Changing the switch combos sometimes failed with "That switch combo isn't valid": the board read a long command in two parts
- "Restore default combos" stayed visible after restoring
- Clicking another combo while recording one left both recording; a click anywhere else now ends recording

## [0.7.2] - 2026-10-15

First public release.

### Added

- One Bluetooth LE keyboard and mouse shared by up to three computers, switched by key combo without reconnecting
- Menu bar / tray app for macOS and Windows (Linux experimental) that installs and updates the board firmware
- Host names and reordering
- 7.5 ms connection interval for the active host, 15 ms fallback
- In-app updates from GitHub Releases
