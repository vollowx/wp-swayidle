# wp-swayidle

A program that manages swayidle depending on audio output.

If any active stream is detected, swayidle will be stopped, if not, swayidle
will be re-spawned.

## Dependencies

### Runtime Dependencies

- swayidle

### Build Dependencies

- meson
- ninja
- GCC or compatible C compiler
- wireplumber
- pipewire

## Installation

    meson setup build
    ninja -C build

    sudo ninja -C build install

## Usage

Run wp-swayidle in the background:

    wp-swayidle <interval> -- <swayidle args>...

For example in `~/.config/sway/config`:

    exec wp-swayidle 3 -- -w timeout 300 'swaylock' \
                             timeout 600 'swaymsg "output * power off"' \
                             resume 'swaymsg "output * power on"' \
                             before-sleep 'swaylock'
