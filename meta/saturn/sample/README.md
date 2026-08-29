# Saturn sample

A 3D scene small enough to check the Saturn export end to end, and the model it
is made of.

| File | What it is |
| --- | --- |
| `Resources/Models/cube.hmdl` | a cube: eight corners, twelve triangles, a colour a corner |
| `Resources/Scenes/cube.scene3d` | three of it, placed, turned and scaled differently |

The model is a binary, so `tools/make-sample-model.py` is what writes it --
a checked-in blob nobody can read is a checked-in blob nobody can review.

```sh
tools/make-sample-model.py meta/saturn/sample/Resources/Models/cube.hmdl
```

The three placements are not decoration. One sits at the origin unrotated, so a
wrong transform shows up as nothing at all; the other two are moved, turned and
scaled by different amounts, so a matrix read with its rows and columns the
wrong way round collapses them onto the first one. That is exactly the bug this
scene caught while the exporter was being written.

To carry it to a Saturn:

```sh
HatchGameEngine --project-dir meta/saturn/sample \
                --export-saturn-3d out Scenes/cube.scene3d
cd out && make
```
