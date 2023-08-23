## Translation
This directory contains .json translation files, which are automatically found at
build time, pre-parsed and embedded in the binary. If a translation is missing,
the program should not crash, instead, an error string will appear (you'll know
what it is when you see it). If built with debug, the application will complain
about missing translations.

### Note
The program expects atleast English to always exist.
