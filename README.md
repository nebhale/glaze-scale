# Glaze Scale

ESPHome touchscreen scale for Glaze Manager.

## Desktop simulator

On macOS install Xcode Command Line Tools, SDL2 (`brew install sdl2`), and the
ESPHome CLI version pinned in `requirements.txt`. Use `brew install esphome` if
it offers that version, or install the pin into a Python virtual environment.
Linux requires a C++ compiler, Git, Make, the SDL2 development package, and the
same CLI. The initial build downloads dependencies.

Run `make sim` from this directory. A 480×320 window opens. Click **Test touch**
and the button changes to **Touch received**. Close the window to exit. Launch
again to reset the demo.
