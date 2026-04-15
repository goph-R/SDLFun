# Level Design Guide (Blender)

How to create levels with diffuse textures and baked lightmaps for the SDLFun FPS engine.

## Prerequisites

- Blender 2.8+ (any modern version)
- The level is exported as Wavefront OBJ with UVs
- Textures are exported as 24-bit BMP files

## Step 1: Model the Level

1. Open Blender, delete the default cube
2. Build your level geometry using basic meshes (cubes, planes)
   - Use `Shift+A` > Mesh > Cube/Plane to add geometry
   - Scale and position to form rooms, corridors, ramps
   - Keep geometry simple - this targets a Pentium 4
3. Join all level pieces into one object (`Ctrl+J`)
4. Make sure normals point inward (into rooms):
   - Enter Edit Mode (`Tab`)
   - Select All (`A`)
   - `Mesh` > `Normals` > `Recalculate Outside`, then `Flip` if needed
   - You can display normals via Overlays > Face Orientation (blue = front, red = back)

## Step 2: UV Unwrap for Diffuse Texture

1. Select your level object, enter Edit Mode
2. Select All (`A`)
3. `UV` > `Smart UV Project` (good default for architectural geometry)
   - Angle Limit: 66 degrees works well
   - Island Margin: 0.01
4. Open UV Editor to verify the unwrap looks reasonable
5. This UV map is used for the diffuse (wall/floor) texture

## Step 3: Create the Diffuse Texture

Option A - Paint in Blender:
1. Switch to Texture Paint workspace
2. Create a new image (e.g., 512x512 or 1024x1024)
3. Paint your wall, floor, ceiling textures directly

Option B - Use tiling textures:
1. Find or create tileable textures (brick, concrete, metal, etc.)
2. Assign materials with these textures to different faces
3. Bake the combined result (see Step 5)

Option C - Simple color materials:
1. Assign different colored materials to walls, floors, ceilings, slopes
2. Bake to texture (see Step 5)

## Step 4: Set Up Lighting for Lightmap

1. Switch to the Rendered viewport or Layout workspace
2. Add lights to your scene:
   - `Shift+A` > Light > Point/Spot/Area
   - Position lights in rooms, corridors, near doorways
   - Adjust intensity and color for mood
3. Optional: Add an HDRI or ambient light for base illumination
4. Use Cycles render engine for best quality bakes:
   - Properties > Render > Render Engine: Cycles

## Step 5: Bake Textures

### Bake Diffuse

1. Select your level object
2. In Properties > Material, make sure materials are set up
3. Create a new Image in the UV Editor (e.g., `diffuse` 1024x1024)
4. In each material's shader node tree, add an Image Texture node
   - Select the `diffuse` image
   - **Make sure this node is selected (highlighted)** but NOT connected
5. Properties > Render > Bake:
   - Bake Type: `Diffuse`
   - Under Influence, check only `Color` (uncheck Direct, Indirect)
   - Click `Bake`
6. Save the image: Image Editor > Image > Save As > **BMP format**
   - Save as `diffuse.bmp` in your SDLFun folder

### Bake Lightmap

1. Create another new Image in UV Editor (e.g., `lightmap` 512x512 or 1024x1024)
2. In each material's shader node tree, select the Image Texture node
   - Change it to the `lightmap` image
   - Again, selected but NOT connected
3. Properties > Render > Bake:
   - Bake Type: `Diffuse`
   - Under Influence, check `Direct` and `Indirect` (uncheck `Color`)
   - This bakes only the lighting, not the surface color
   - Click `Bake`
4. Save as `lightmap.bmp` in your SDLFun folder

## Step 6: Export OBJ

1. Select your level object
2. `File` > `Export` > `Wavefront (.obj)`
3. Export settings:
   - Check: `Selection Only`
   - Check: `Write Normals`
   - Check: `Include UVs`
   - Forward: `-Z Forward`
   - Up: `Y Up`
   - Uncheck: `Write Materials` (we handle textures ourselves)
4. Save as `test_level.obj` (or your level name) in the SDLFun folder

## Step 7: Run the Game

Place these files in the SDLFun folder:
- `test_level.obj` - the level geometry with UVs
- `diffuse.bmp` - the wall/floor texture atlas (24-bit BMP)
- `lightmap.bmp` - the baked lighting (24-bit BMP)

The engine automatically loads `diffuse.bmp` and `lightmap.bmp` if present.
Without them, it falls back to colored rendering with dynamic OpenGL lighting.

## Tips

- **Texture size**: 512x512 is fine for a P4, 1024x1024 is the practical max
- **Keep geometry low-poly**: Aim for under 5000 triangles per level
- **Slopes**: Any angle up to 50 degrees is walkable by the player
- **Player spawn**: The player spawns at (0, 2, 0) - make sure there's floor there
- **Scale**: 1 unit = 1 meter. Player is 1.75m tall. Standard room height is ~3-4m
- **Test often**: Export and run the game to check scale and feel
- **Lightmap resolution**: Lower res (256x256) gives a softer, more retro look
- **BMP format**: Save as 24-bit, uncompressed. The engine does not support RLE or other compression

## File Format Notes

The engine reads standard Wavefront OBJ with these features:
- Vertices (`v x y z`)
- Texture coordinates (`vt u v`)
- Normals (`vn x y z`)
- Faces (`f v/vt/vn v/vt/vn v/vt/vn`) - triangles and quads (auto-triangulated)

Both BMP (24/32-bit) and TGA (24/32-bit uncompressed) textures are supported.
