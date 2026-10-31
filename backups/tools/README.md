# tools — regenerating the skinchanger data tables

`src/data/skindata.h` and `src/data/particledata.h` are generated from TF2's own files,
not hand-typed. Rerun these after a TF2 update that adds weapons or unusual effects.

Inputs (paths are hard-coded at the top of each script):

- `tf/scripts/items/items_game.txt` — item definitions, model paths, style skins, attributes
- `tf/resource/tf_english.txt` — the `Attrib_Particle<n>` display names

```
cd cplus/tools

# particle list -> src/data/particledata.h
python parse_particles.py particles.json
python gen_particledata.py ../src/data/particledata.h

# reskin models / anim slots / style skins -> the tail of src/data/skindata.h
python gen_reskininfo.py reskininfo.inc
#   then replace everything from the "Reskin runtime info, generated" banner
#   to the end of src/data/skindata.h with reskininfo.inc
```

`gen_reskininfo.py` walks each item's `prefab` chain, so `model_player` and `anim_slot`
come out right even for items that inherit them. `items_game.py` is the shared parser —
run it directly with a list of def indexes to dump what the schema says about them:

```
python items_game.py 423 1071 169
```

Everything above the generated banner in `skindata.h` (the weapon list, the warpaint
names, the reskin groups, the weapon-specific skin defs) is still hand-maintained.

# crosshair blend modes

`src/features/fx_chblend_ps.hlsl` holds the custom crosshair's blend modes (Krita's
formulas). Its header is generated, one ps_3_0 per mode:

```
cd cplus
bash src/features/fx_chblend_build.sh     # -> fx_chblend_ps.h + fx_chblend_vs.h
bash tools/chblend_test/run.sh            # every mode on the GPU vs Krita's own code
```

`run.sh` fetches Krita's blend headers at a pinned commit, builds them against the tiny Qt
stubs in `chblend_test/stubs`, renders each shader over 65536 src/dst pairs through the same
vertex path the DLL uses, and fails any pixel more than 2 steps off Krita's 8-bit result.
Adding a mode: append to `ch_blend_modes.h` (ids are saved in cfgs, never reorder), add its
formula to the hlsl dispatch, bump `LAST` in the build script, add its reference in
`chblend_test.cpp`. The same files live in fpsoverlay; copy them across after a change.
