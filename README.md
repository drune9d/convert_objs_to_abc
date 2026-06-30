<div align="center">

# OBJ Sequence → Alembic

**Free app to convert an OBJ sequence to Alembic (`.abc`) — a batch OBJ to ABC
mesh cache converter for macOS and Windows.**

![Platform](https://img.shields.io/badge/platform-macOS%2013%2B%20%7C%20Windows%2010%2F11-black)
![License](https://img.shields.io/badge/license-MIT-blue)
![Format](https://img.shields.io/badge/output-Alembic%20.abc-orange)

<img src="docs/gui.png" alt="OBJ sequence to Alembic converter GUI" width="640">

</div>

---

Batch-convert a folder of numbered OBJ files into a single animated Alembic
(`.abc`) mesh cache — with UV support, changing-topology meshes, and fast
parallel conversion. Drag in a folder of OBJs, get one `.abc` you can import into
**Blender, Maya, Houdini, or Cinema 4D**.

This tool is meant for folders full of numbered OBJ files, such as:

```text
0000.obj
0001.obj
0002.obj
...
```

## Choose your platform

This repository ships a native app for each operating system. Pick the folder for
your OS — each one is self-contained, with its own app, build scripts, and
instructions.

| Platform | Folder | Start here |
| --- | --- | --- |
| 🍎 **macOS** 13+ | **[`macos/`](macos/)** | [macOS guide](macos/README.md) — download `OBJ Sequence to Alembic.app` |
| 🪟 **Windows** 10/11 | **[`windows/`](windows/)** | [Windows guide](windows/README.md) — download `OBJ Sequence to Alembic.exe` |

Prebuilt downloads are published on the
[GitHub Releases](../../releases) page (macOS `.zip` and Windows `.zip`). End
users do not need Python, CMake, or a compiler to run a release build — those are
only needed if you build from source.

## What it exports

| Data | Exported |
| --- | :---: |
| Animated vertex positions | ✅ |
| Face indices and face counts | ✅ |
| Face-varying UVs (when OBJ `vt` data exists) | ✅ |
| Framerate / time sampling | ✅ |
| Per-frame changing topology | ✅ |
| Normals | ❌ |
| Materials or `.mtl` files | ❌ |
| Texture paths | ❌ |
| OBJ groups or object names | ❌ |
| Vertex colors | ❌ |
| Multiple independent meshes in one OBJ | ❌ |

## Changing topology

Each frame is written with its own topology, so sequences whose vertex and face
counts change over time — fracture, fluid, or remeshing simulations — are
preserved correctly rather than frozen to the first frame. Control file size by
exporting fewer frames or simplifying the mesh upstream in your DCC.

## Command line

Both platforms include the same converter as a command-line tool. From inside the
platform folder:

```bash
# macOS
bin/Objs2Abc -i head-poses -o output/head-poses.abc -f 24 -n Head
```

```powershell
# Windows
.\bin\Objs2Abc.exe -i .\head-poses -o .\output\head-poses.abc -f 24 -n Head
```

## Keywords

OBJ to Alembic · convert OBJ sequence to Alembic · OBJ to ABC converter · batch
OBJ converter · OBJ sequence to ABC · Alembic mesh cache · OBJ animation to
Alembic · macOS Alembic converter · Windows Alembic converter · OBJ frame
sequence · changing topology Alembic · Blender / Maya / Houdini / Cinema 4D
Alembic import.

## Credits

This project builds on the original
[convert_objs_to_abc](https://github.com/ziyeshanwai/convert_objs_to_abc) by
Liyou, which provides the core OBJ-to-Alembic conversion. This fork adds native
macOS and Windows apps and command-line builds, a Tk GUI with progress reporting,
changing-topology support, and a number of correctness and performance fixes.

## License

Released under the MIT License. See [LICENSE](LICENSE). Original work
Copyright (c) 2022 Liyou.
