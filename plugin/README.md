# SoundNuts II — the plugin

The JUCE wrapper around the JUCE-free modulation engine in [`../dsp`](../dsp). The trajectory of a
strange attractor drives a filter, tremolo and auto-pan on the input; the **Macro** walks the
bifurcation diagram; the regime strip and the phase-space scope show where you are.

## Controls

- **System** browser with search and starred favourites; **system morph** crossfades between two
  systems (ms or beats) with the engines pre-built, so switching never allocates on the audio thread.
- **Macro**, **Rate**, **Filter** base and amount, **Resonance**, **Tremolo**, **Auto-pan**,
  **Smoothing**, **Solver** (auto, numerical, analytic), **On blow-up** (reset or free-clamp),
  **Mix**, **Output**.
- **Orbit tempo-sync**: lock one characteristic orbit to a beat division; bar-locked orbit reset.
- **LFO A / B** with eight shapes, free or synced rate, transport phase-lock, bipolar or unipolar,
  and bounded depths onto Macro, Filter base, Tremolo and Rate.
- Six factory presets and named user presets.

## Install a release (no build needed)

Download `SoundNuts-II-2.0.0-macOS.zip` from the [Releases](https://github.com/MarioKartUltra/SoundNuts/releases)
page and unzip it. The bundles are signed ad hoc rather than notarised, so macOS quarantines them
after the download. Clear that first, then copy them into the user plug-in folders:

```sh
xattr -dr com.apple.quarantine "SoundNuts II.vst3" "SoundNuts II.component" "SoundNuts II.app"
cp -R "SoundNuts II.vst3"      ~/Library/Audio/Plug-Ins/VST3/
cp -R "SoundNuts II.component" ~/Library/Audio/Plug-Ins/Components/
```

Then rescan in the DAW (Ableton Live: Settings → Plug-Ins → Rescan; Logic and GarageBand pick up
the AU after a restart). `SoundNuts II.app` is the standalone version; pick an input device in its options. macOS 11 or later,
Apple Silicon or Intel.

## Build from source (macOS)

Xcode command-line tools and CMake 3.22 or later (`brew install cmake ninja`).

```sh
git clone https://github.com/MarioKartUltra/SoundNuts.git
cd SoundNuts/plugin
./build.sh                 # arm64
./build.sh --universal     # arm64 + x86_64
```

The first configure fetches JUCE 8.0.7 and the patched foleys_gui_magic (network, once). To build
against checkouts you already have, pass `-DJUCE_DIR=... -DFOLEYS_DIR=...` or symlink them as
`external/JUCE` and `external/foleys_gui_magic`.

Artefacts land in `build/SoundNutsII_artefacts/Release/`. By default the VST3 and AU are also copied into `~/Library/Audio/Plug-Ins`; pass
`-DSOUNDNUTS_INSTALL_AFTER_BUILD=OFF` to keep them in the build directory. `-DSOUNDNUTS_GUI_EDITOR=ON`
opens foleys' WYSIWYG layout editor beside the plugin, for development only.

```sh
ctest --test-dir build --output-on-failure    # the offline DSP checks
auval -v aufx Snt2 Sdnt                      # validate the installed AU
```

## Licence

SoundNuts II is free software under the GNU Affero General Public License, version 3 or later
(`../LICENSE`). Third-party components and their licences are listed in `../NOTICE`; JUCE is used under
its AGPLv3 option.
