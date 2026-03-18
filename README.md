<p align="center">
  <img src="docs/banner.png" alt="topaz pan" width="400">
</p>

<p align="center">
  An extremely simple vocal widener plugin.
</p>

<p align="center">
  <strong>English</strong> · <a href="./README.ja.md">日本語</a>
</p>

<p align="center">
  <a href="https://github.com/Pachii/topaz-pan/releases">
    <img src="https://img.shields.io/github/v/release/Pachii/topaz-pan?include_prereleases&display_name=tag&sort=semver&style=for-the-badge" alt="Latest Release">
  </a>
  <a href="https://github.com/Pachii/topaz-pan/actions/workflows/build-vst3.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/Pachii/topaz-pan/build-vst3.yml?style=for-the-badge&label=build" alt="Build Status">
  </a>
  <img src="https://img.shields.io/badge/formats-VST3%20%7C%20AU-111827?style=for-the-badge" alt="Plugin Formats">
  <img src="https://img.shields.io/badge/license-MIT-111827?style=for-the-badge" alt="MIT License">
  <a href="https://github.com/Pachii/topaz-pan/stargazers">
    <img src="https://img.shields.io/github/stars/Pachii/topaz-pan?style=for-the-badge" alt="GitHub Stars">
  </a>
</p>

<p align="center">
  <a href="https://github.com/Pachii/topaz-pan/releases">releases</a>
  ·
  <a href="#why-this-exists">why this exists</a>
  ·
  <a href="#installation">installation</a>
  ·
  <a href="#building-from-source">build from source</a>
  ·
  <a href="#contributing">contributing</a>
</p>

---


A tiny plugin for a very specific job: widening vocal harmonies without managing duplicate tracks.

It gives you Haas-based widening, optional ADT-style drift and decorrelation, L/R panning, optional channel balance compensation, and an `advanced` section for the extra controls.



## Why This Exists

This plugin is designed to easily replicate a background vocal panning technique used by many Japanese cover artists. The result is convincingly wide-sounding vocal harmonies.

The technique itself is actually very simple:

1. Duplicate the vocal track
2. Nudge the timing of one track slightly
3. Pan the two tracks hard left and right

If that sounds too simple to be real, take a look at this mixing video by Mafumafu where he does exactly this with nothing fancy added:

https://www.youtube.com/watch?v=pwHqy9cKi7c&t=2604s

Even if you have no idea what genre or artists I'm talking about, that's completely fine. This plugin works for any situation where you want to widen background vocals/harmonies as simply and naturally as possible. I'm just mentioning the Japanese cover scene because this technique is extremely common there.

Now for the sad backstory: to use this technique, the normal DAW workflow is very annoying:

- duplicate the harmony track
- zoom in a lot and nudge the timing very precisely
- pan both tracks
- then if something needs fixing, fix it twice because now there are duplicate tracks everywhere (Melodyne, vocal alignment, etc.)
- Be sad in general because now you have a bunch of extra tracks cluttering your DAW

There are already plugins that """"do this"""", but they never quite felt like the exact answer for this extremely simple problem.

- **iZotope Vocal Doubler** exists, but its settings are neither flexible, transparent, or simple. You end up with vocals that have weird phasing issues and wobbling pitch, and crazy distortion issues in some cases. If you've been using this, go back to a previous project and solo the tracks using them and you might be surprised how strange it sounds.

- **Soundtoys MicroShift** is a better alternative, but it is 100 dollars and not built with functions to cater specifically to vocal applications. This plugin, on the other hand, is free.

The gist of the problem is that there does not seem to be a plugin that literally only duplicates and time shifts the audio, and nothing else. So I decided to make one.

Every parameter clearly shows exactly what it is doing to the audio. There are no hidden modulation tricks or mystery processing. Additional features are available if you want them, but every extra feature can be completely disabled. In fact, every additional feature is off by default for users who want a one-to-one replication of the original process.

That means you can reduce the plugin down to exactly the same result as manually duplicating and nudging the track in your DAW, but without the hassle of managing duplicate tracks.

---

## Screenshot

<img src="docs/screenshot.png" alt="topaz pan interface" width="400">

---

## How It Works

At a high level, the plugin combines three simple cues:

- a small timing difference
- controlled left/right placement
- optional level balance compensation

That combination is enough to trick your brain into making harmonies and doubles feel wide.

### Haas effect

The "Haas effect" is the main idea behind the widening. If one side arrives a little later than the other, your ear hears width and direction before it hears an obvious echo.

### Haas comp (compensation)

When you delay one channel, it can make the undelayed channel sound louder, creating a perceived imbalance. This control adjusts the volume of the left and right channels simultaneously based on math to keep the image feeling balanced.

`haas comp` is an experimental feature and subject to change. It is off by default in `advanced` for users who want the basic manual process first, and is there for people who want to enable it and adjust it to taste.

---

## Controls

By default, the main screen only shows `offset time`, `output gain`, and `bypass`.
Click `advanced` to reveal the rest of the controls.

| Control | Description |
| --- | --- |
| `offset time` | Sets the Haas delay offset used for width. The default is `15 ms`. |
| `output gain` | Controls final output level. |
| `left pan` / `right pan` | Places the two sides in the stereo field. These are linked by default for convenience. |
| `adt drift` | Adds ADT-style drift and decorrelation between channels. Experimental feature, subject to change. Off by default in `advanced`. |
| `haas comp` | Controls how strongly the plugin compensates for the perceived level imbalance caused by the Haas effect. Experimental feature, subject to change. Off by default in `advanced`. |

| Toggle | Description |
| --- | --- |
| `equal delay` | Centers the timing offset. Instead of delaying only one side, it splits the timing correction so the core signal stays more rhythmically centered. This introduces a little extra (compensated) latency when enabled. |
| `link pan` | Links the left and right panning. If this is off, the left and right channels can be panned independently. Honestly, I'm not sure why you would ever want to turn this off. |
| `flip pan` | Flips the left and right channels. |
| `haas comp` | Enables or bypasses Haas compensation. |
| `bypass` | Bypasses the plugin. |

### Quick starting point

1. Add topaz pan to the vocal track or bus that you want to widen. The plugin accepts mono or stereo input and outputs stereo.
2. Change `offset time` to your liking. A sweet spot range is still around `10-25 ms`, and the default now starts at `15 ms`.
3. If you need more control, open `advanced` and adjust pan, ADT drift, or Haas compensation.

### About ADT drift

`adt drift` is based on artificial double tracking. Instead of only delaying one side, it adds tiny pitch movement and timing variation between the left and right channels. The idea is to slightly decorrelate the two sides so they do not line up quite as perfectly.

It is an experimental feature and subject to change. It is off by default in `advanced` for users who want a closer one-to-one version of the original manual process, and is there for people who want to enable it and adjust it to taste. Adds slight (compensated) latency.

---

## Installation

Downloads are available on the [releases page](https://github.com/Pachii/topaz-pan/releases).

### macOS

- **VST3**: `/Library/Audio/Plug-Ins/VST3`
- **AU**: `/Library/Audio/Plug-Ins/Components`

### Windows

- **VST3**: `C:\Program Files\Common Files\VST3`

### Linux

- **VST3**: `~/.vst3` or `~/.local/share/vst3`

Linux builds are currently provided as an experimental convenience and are **untested**.

### Installers

Current releases include:

- macOS installer for AU (Logic Pro) and VST3
- Windows installer for VST3
- Linux `.zip` downloads for VST3
- `.zip` downloads for manual plugin installation

---

## Building From Source

This project uses **CMake** and **JUCE**.

JUCE is fetched automatically during configure in the current setup.

```bash
cmake -S . -B build
cmake --build build
```

### Requirements

- CMake
- a C++20-capable toolchain
- macOS: Xcode / Apple Clang
- Windows: Visual Studio / MSVC recommended for JUCE plugin builds
- Linux: GCC or Clang with the required X11 / audio development packages

### Build outputs

Depending on platform, the project builds:

- `VST3`
- `AU`
- `Standalone`

---

## Contributing

Contributions are welcome.

If you find a bug, have a feature idea, or want to improve the DSP or UI, open an issue or send a pull request. If you are reporting a bug, include:

- DAW
- operating system
- plugin format
- a clear description of what happened
- steps to reproduce it if possible

If you want to make a larger change, opening an issue first is appreciated.

---

## Releases

Releases live here:

[https://github.com/Pachii/topaz-pan/releases](https://github.com/Pachii/topaz-pan/releases)

They currently include:

- macOS VST3
- macOS AU
- Windows VST3
- Linux VST3
- macOS installer
- Windows installer

The Linux build is currently **untested**.

---

## License

This project is released under the MIT License. See [LICENSE](LICENSE) for the full text.

---

<p align="center">
  built with JUCE
</p>

<p align="center">
  <a href="https://github.com/Pachii/topaz-pan">github.com/Pachii/topaz-pan</a>
</p>
