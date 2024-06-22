# About
I made this as a one-off project for my family, specifically for my uncle.

It's a simple YouTube video downloader, made specifically for Microsoft Windows©,
and for technologically illiterate users.

# Further development and contributions
I don't plan on developing this any further, unless I have a personal reason
to do so, like the program breaking for my uncle, or a feature request by him.

I will probably respond to simple issues or pull requests, but don't expect me
to spend an extended amount of time on this, feel free to fork it in that case
and work on this yourself, if you're inclined to do so.

# Zig version
Currently tested and built using: `0.14.0-dev.112+9be9b8ca9`

# Using clangd for completions
You can create compile_commands.json while building using -Dgenerate-ccjson=true flag.

# Compiling
Compilation was made specifically for Linux, you may have to change it
to compile on different operating systems or architectures.

## Dependencies
Dependencies are automatically downloaded and compiled by `build.zig`.
This is why you need a very recent (at the time of writing) master-branch version of Zig.

# Packaging
The program needs a few things at runtime in the same directory as it's located,
NOT THE CURRENT DIRECTORY.

You also probably want to include the licenses of imgui, gltf, cpp-json and yt-dlp.

## yt-dlp.exe
Obtain `yt-dlp.exe` from its [github](https://github.com/yt-dlp/yt-dlp).
I specifically tested it with release `2023.11.16`.
The executable name must be **exact**.

## font.ttf
This isn't strictly necessary, but most of the text won't display properly without it.
I recommend [Iosevka](https://typeof.net/Iosevka/), it's my favorite font
and it supports a lot of glyphs.
