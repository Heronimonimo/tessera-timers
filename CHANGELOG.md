# Changelog

## 0.4.0
- A new sensor in Home Assistant, **Timers done**, names the timer helpers whose timer is done, such as
  `timer.eggs, timer.rice`. A helper leaves the list when you tap Stop, +1m or Cancel on its tile, so an automation can
  tell several timers on one screen apart, for example to stop the right alarm on a speaker. The **Timer done** binary
  sensor stays as it was.
- A new button in Home Assistant, **Silence timers**, does what Stop does on every timer of the screen that is done:
  the ringing stops and the timer goes back to idle. An automation that stopped its own alarm another way (a button on
  a phone's notification, a speaker's button) can silence the screen with it. Running timers run on.

## 0.3.1
- Nothing changes on the screen. The README says the plugin was tried on a real screen, the M5Stack Tab5, and the
  plugin is now checked against Tessera 0.4.90.

## 0.3.0
- The top bar can show a running timer and a running stopwatch: in Layout, add **Running timer** or **Running
  stopwatch** to a page's top bar, under From plugins. It shows only while one runs, in whole minutes ("6 min"), and
  turns red with "Done" when a timer is done. With several, the timer that ends first and the stopwatch that runs
  longest.
- The Stopwatch tile's icon is one of Tessera's own now (it showed none).

## 0.2.1
- A switch for the sound: **Timer sound** in the plugin's settings, on the screen's settings page and in Home Assistant,
  on a screen with a speaker. Turning it off silences a timer that rings at once.

## 0.2.0
- A timer that is done now rings on a screen with a speaker: three beeps every two seconds for a minute, or until you
  tap Stop. It is the part "Ring when a timer is done" in the plugin's details, on by default; it follows the screen's
  volume. Screens without a speaker build as before.

## 0.1.0
- First version: a timer you set with +10s, +1m, +10m and +1h, on the screen or as a timer helper of Home Assistant,
  and a stopwatch with laps. Both keep running through a restart of the screen.
