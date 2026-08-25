# Mega Drive export sample

A scene small enough to read and awkward enough to be worth testing: eight
16x16 tiles laid over a 32x24 map, drawn so the conversion has something to
chew on.

- Solid, checkered, bordered and banded tiles, so cells collapse at different
  rates when they are deduplicated.
- A horizontal ramp, so a tile's two halves differ and cannot share a pattern.
- A diagonal and its mirror image, which cost one pattern between them: a
  nametable entry carries a flip bit for each axis.

```sh
HatchGameEngine --project-dir meta/megadrive/sample \
                --scene Scenes/sample.tmx \
                --export-megadrive /tmp/sample-md
```

That writes an SGDK project. The **Mega Drive** section of the repository's
README covers building it into a ROM.
