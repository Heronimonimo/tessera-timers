# Changelog

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
