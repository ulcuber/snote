# 📝 snote - Suckless Floating Text Widget

A lightweight, floating text widget for X11 that stays on top of your windows. Perfect for quick notes, reminders, or displaying information on your desktop.

## ✨ Features

- 🎯 **Floating** - Stays on top of windows, doesn't interfere with workflow
- 🎨 **Transparent** - Clean, unobtrusive appearance with alpha support
- 📝 **Customizable** - Font size, position, colors via Xresources
- 🖱️ **Draggable** - Move anywhere with left-click drag
- ⌨️ **Keyboard friendly** - Close with `Escape` or `q`
- 📦 **Low memory footprint** - ~(1.9MB+9.3MB) Private+Shared per instance

## 🚀 Installation

### From Source

```bash
make
make install
```

### Dependencies

- X11 libraries (`libX11`, `libXext`)
- Xft (`libXft`)
- Fontconfig (`fontconfig`)
- FreeType (`freetype2`)

**On Artix/Arch:**
```bash
sudo pacman -S libx11 libxext libxft fontconfig freetype2
```

**On Debian/Ubuntu:**
```bash
sudo apt install libx11-dev libxext-dev libxft-dev libfontconfig-dev libfreetype-dev
```

## 🎮 Usage

```bash
snote [OPTIONS] "text"
snote -h
```

### Examples

```bash
# Simple note
snote "Hello, world!"

# Positioned with custom font size
snote -x 200 -y 300 -s 20 "Hello, World!"

# Sticky note with box
snote -S -a -b "Important Note"

# Using a file
snote "$(cat ~/Documents/note.txt)"

# Multiline
snote "$(printf "Hello\nWorld")"
snote "$(echo -e "Hello\nWorld")"
snote "$(fortune)"
```

## 🎨 Theming

### Xresources Configuration

Add these to your `~/.Xresources`:

```xresources
! ── snote colors ──
*.foreground: #5a3d5c
*.background: #f5ebf0
*.color1: #ff7b9c
*.faceName: ComicShannsMono Nerd Font
*.fontSize: 16
```

## 🔧 i3 Integration

You can add these to your `~/.config/i3/config`:

```conf
	bindsym n exec --no-startup-id ~/.config/rofi/snote, mode "default"
	bindsym Shift+n exec --no-startup-id ~/.config/rofi/snote -b, mode "default"
```

### Wrapper Script with Rofi

Create `~/.local/bin/snote-rofi`:

```bash
#!/bin/bash

xkb-switch -s us

text=$(rofi -dmenu \
    -p "📝 Note: " \
    -theme-str 'window {width: 50%; height: 6em;}' \
    -hide-scrollbar)

if [ -n "$text" ]; then
    snote "$@" "$text"
fi
```

## 🔗 Related

- [i3wm](https://i3wm.org/) - Recommended window manager
- [Picom](https://github.com/yshui/picom) - X compositor for transparency
- [Rofi](https://github.com/davatorium/rofi) - Input dialog integration

---

Made with ❤️ and 🌸✨
