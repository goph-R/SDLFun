# Combining Mixamo animations into one IQM

IQM is designed for this. A single `.iqm` can hold one mesh + skeleton + N animations. Two common paths:

## Path A — Blender (easiest for Mixamo)

1. Install the IQM exporter addon for Blender (lsalzman's `blender-iqm` or the Sauerbraten/Red Eclipse fork that supports 2.8+).
2. Import the first Mixamo FBX. This gives you an armature + mesh + one Action.
3. In the Action Editor, click the "shield" (Fake User) icon on that Action so it's not purged when it loses users. Rename it to something short like `idle`, `run`, `jump`.
4. Import the next FBX. It comes in as a new armature+action. Move the new Action onto your *original* armature (assign via the Action Editor dropdown), shield it, rename it, then delete the duplicate armature+mesh that came with the FBX. The skeletons are identical so this is safe.
5. Repeat for each animation. You end up with one armature and a stack of NLA/actions.
6. Select the mesh + armature and `File → Export → Inter-Quake Model`. In the exporter panel there's an "Animations" field — list the action names comma-separated (e.g. `idle, run, jump`) or leave blank to export all shielded actions. Frame rate and loop flags are per-action.

## Path B — `iqmtool` command line

Export each animation from Blender as `.iqe` (text IQM) or separate `.iqm`, then:

```
iqmtool out.iqm mesh.iqe idle.iqe run.iqe jump.iqe
```

`iqmtool` concatenates the animation blocks; the mesh/skeleton comes from the first file that has one.

## Mixamo gotchas

- Download all clips with the *same* skeleton option ("With Skin" on the first, "Without Skin / In Place" on the rest works fine — but keep "In Place" consistent or your root motion will differ between clips).
- Mixamo bone names are prefixed `mixamorig:` — the `iqm.h` loader doesn't care, but if you ever retarget or hand-author bone lookups, strip the prefix in Blender (`Select All Bones → F2 → replace`).
- Mixamo exports at 30 fps by default; set the framerate explicitly per-action in the exporter so playback speed is deterministic.

Then in-engine you just pick animation index 0/1/2/... from the same loaded IQM.
