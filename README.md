LiveFive is a YAFIYGI HTML editor with real-time previewing.

## Compiling

Until I set up Autotools, you can use:

```
gcc livefive.c -o livefive $(pkg-config --cflags --libs gtk+-3.0 gtksourceview-3.0 webkit2gtk-4.0)
```

## BUGS

Valgrind says it leaks. The more times you create or open documents,
the more likely it seems to be to crash arbitrarily. I don't know where
the problem is.

## TODO

In no particular order:

* Save and load the user's grid layout.
* Allow the user to resize the columns/rows.
* Keyboard shortcuts.
* Template code.
* User preferences for indentation (fake tabs with spaces, width).
* Allow loading straight from a URL, and following links.
* Set up autotools.
