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
Currently tested and built using: 0.11.0-dev.3132+465272921

# Using clangd for completions
Clangd wants to know how the project was compiled to find headers and stuff,
but [Zig isn't exactly best at providing that](https://github.com/ziglang/zig/issues/9323),
so we will have to aid it a little in creating `compile_commands.json`, which you might notice
is an invalid symlink in the project by default.

## Creating compile_commands.json
0. Run make.jl or perform the same steps as it performs.
1. In build.zig, add the flags `-MJ build/compile_commands.json` to the `exe` build step
(they're already there as comments, uncomment them).
2. Run a build (it can fail), which will create the compile_commands.json file.
3. Edit the compile_commands.json file to be a valid JSON array, by adding square brackets
and deleting the comma at the end of the file
4. Remove (or comment out) the flag from step #1, otherwise it will overwrite our edited
compile_commands.json with invalid compile_commands.json.
5. You should now have working clangd completions.

# Compiling
Compilation was made specifically for Linux, you may have to change it
to compile on different operating systems or architectures.

## Dependencies
There are a few dependencies, there are three ways to download them, manually,
or using the convenient download.sh posix script, which uses curl or wget
to fetch the sources and tar to decompress them (GNU tar specifically, but
it should work with most other implementations, with busybox being an outlier
and possibly not working).

The third way is built directly into zig build, but that way is unstable
and may result in panics and a corrupted download, use with caution.

After obtaining dependencies, simply running `zig build` should be enough to build.

# Packaging
The program needs a few things at runtime in the same directory as it's located,
NOT THE CURRENT DIRECTORY.

You also probably want to include the licenses of imgui, gltf, cpp-json and yt-dlp.

## yt-dlp.exe
Obtain `yt-dlp.exe` from its [github](https://github.com/yt-dlp/yt-dlp).
I specifically tested it with release `2023.01.06`.
The executable name must be exact.

## font.ttf
This isn't strictly necessary, but most of the text won't display properly without it.
I recommend [Iosevka](https://typeof.net/Iosevka/), it's my favorite font
and it supports a lot of glyphs.
