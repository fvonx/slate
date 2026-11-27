# Slate

![Slate with a small plan sketched on it](share/screenshot.png)

Sketch shapes and the connections between them on an infinite canvas, for Omarchy. Rectangles, circles, triangles, rhombuses, arrows, post-its, lines, and text, each of which can be turned; connections that stay attached while you move things around; as many boards as you like, each saved on its own as you work.

Everything stays on this machine, in `~/.local/share/slate/boards`, one JSON file per board.

## Using it

Press a key, drag, done. Drawing a shape drops you back into selecting, so the next thing you do is move it or write on it.

- **Draw**: `R`, `O`, `T`, `D`, or `L`, then drag; the same five are in the toolbar's shapes menu. A click instead of a drag drops the shape at a comfortable size. Hold Shift for a square, a true circle, or a straight line (level, upright, or at 45°); Shift on a line's end keeps it straight too.
- **Frames**: `F`, or the shapes menu, and drag to draw a frame: a named area that holds what lies inside it. Move the frame, by its name or once it is selected, and everything in it moves along; delete or copy it and its contents go too. Double-click the name to rename it. A drag inside a frame that isn't selected draws a selection band, and a click there selects the frame.
- **Icons**: `I` opens a menu of icons (star, check, warning, idea, person, clock and more); pick one and click or drag on the canvas. Icons move, size, turn, color, and connect like any other shape.
- **Images**: `M`, or the picture in the toolbar, asks for image files (PNG, JPEG, WebP, GIF, BMP, SVG) and puts them in the middle of the view; files dragged in from the file manager land where you drop them. An image moves, resizes, turns, groups, and lines up like any shape, and connections attach to it. Slate keeps its own copy, in `~/.local/share/slate/boards/images`, so the board still shows it when the original is moved or deleted. Tap `Space` with an image selected to look at it large over the board, at its own size or as large as the window has room for; `Space`, `Esc`, or a click puts it away.
- **Post-its**: `P` and a click sticks one under the pointer, ready to type on.
- **Resize**: drag a corner of the selected shape, or the outline of any shape, selected or not, to change just its width or its height. The pointer turns into a resize arrow wherever that works. Hold Shift to keep the shape's proportions. With several shapes selected, the box around them resizes them all together.
- **Turn**: hold `Alt` and drag any handle of the selected shape to rotate it. It catches on every 45° and on the angle of any other shape, and shows the angle beside the pointer. Shift turns in steps of 15°, `Ctrl` turns freely, and `,` and `.` turn from the keyboard.
- **Snap**: a shape you drag catches on the sides and middles of the shapes it passes, and so does a side or corner you resize, with a guide line showing what it lined up with. Hold `Ctrl` to place or size it freely.
- **Border style**: the button beside the colors makes the outline of the selected rectangles, circles, triangles, rhombuses, arrows, frames, lines, and connections solid, dashed, or dotted; `S` steps through the three. New shapes take the style last picked.
- **Arrange**: select several shapes and press `Alt` with an arrow to line them up on that side; with three or more, `Alt+Shift+H` and `Alt+Shift+V` space them evenly between the outermost two, across or up and down; `]` and `[` bring the selection to the front or send it to the back. The toolbar's arrange menu has the same.
- **Group**: `Ctrl+G`, or the arrange menu, makes one group of the selected shapes: a click on any of them selects them all, so they move, resize, line up, and go to the front or back as one, and a copy is a group of its own. `Ctrl+Shift+G` takes the group apart. Grouping groups makes one group of them all.
- **Write**: double-click a shape to label it, or empty canvas for free text. Enter finishes, Shift+Enter starts a new line.
- **Connect**: hover a shape and drag from one of the dots on its sides to another shape. Let go over empty canvas and a connected copy of the shape appears there, ready to be labelled. `C` does the same from anywhere on a shape. The connect button's menu says where the arrow is: at the end you drew to, at the start, or nowhere. It changes the selected connections, or else the ones you draw next; `A` steps through the three.
- **Export**: `PDF` and `SVG`, beside the board's name, save the board to your Downloads folder, drawn as it looks in the window.
- **Move around**: the mouse wheel zooms on the pointer; two fingers on a touchpad pan, and pinch zooms. Hold Space, or the middle or right button, and drag to pan.

| Key | Action |
| --- | --- |
| `R` `O` `T` `D` `W` `L` | Draw a rectangle, circle, triangle, rhombus, arrow, or line |
| `F` | Draw a frame |
| `P` | Stick a post-it |
| `I` | Place an icon |
| `M` | Add images from files |
| `X`, or `T` twice | Write text |
| `C` | Connect two shapes |
| `A` | Move a connection's arrow to its other end, or take it away |
| `V`, `Esc` | Select and move |
| `Enter` | Write on the selected shape |
| `1` – `8` | Color the selection (1 is no color); the same colors are in the toolbar menu |
| `S` | Make the border solid, dashed, or dotted; the same styles are in the toolbar menu |
| `,` `.` | Rotate the selection by 15° |
| `Delete` | Delete the selection |
| `Ctrl+C`, `Ctrl+V` | Copy and paste, under the pointer |
| `Ctrl+D` | Duplicate |
| `Ctrl+A` | Select everything |
| `Ctrl+Z`, `Ctrl+Shift+Z` | Undo and redo |
| Arrows | Nudge the selection (Shift for more) |
| `Alt` + arrows | Align the selected shapes on that side |
| `Alt+H` `Alt+V` | Center the selected shapes on each other, horizontally or vertically |
| `Alt+Shift+H` `Alt+Shift+V` | Space three or more selected shapes evenly, horizontally or vertically |
| `Ctrl+G` `Ctrl+Shift+G` | Group the selection, or take the group apart |
| `]` `[` | Bring to the front, send to the back |
| `+` `-` `0` | Zoom in, out, and back to 100% |
| `Shift+F` | Fit everything in the window |
| `Space` | Hold and drag to pan |
| `Space` | Tap with an image selected to see it large; again, or `Esc`, to put it away |
| `B` | Open the boards menu at the top left |
| `Ctrl+N` | New board |
| `Ctrl+Tab` | Next board |
| `F2` | Rename this board |
| `?` | Show or hide the shortcuts |
| `Q` | Quit |

Slate follows the Omarchy theme (colors are stored as palette names, so a board changes with the theme), the window rounding, and the language picked under Setup > Language.

## Building

```bash
bin/build                                  # into build/
bin/test                                   # builds, then runs the tests offscreen
cmake --install build --prefix ~/.local    # into the app launcher
```

It needs `cmake`, a C++ compiler, and Qt 6 (`qt6-base`, `qt6-declarative`, `qt6-svg`, `qt6-wayland`; `qt6-imageformats` for WebP images), all part of Omarchy.

## License

MIT. The theme, translator, and build scaffolding come from the Omarchy apps by David Heinemeier Hansson.
