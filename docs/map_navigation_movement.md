# Technical Reference: Minimap Clicks, World Navigation & Character Movement

This document serves as a complete architectural and mathematical reference mapping out the code, systems, and equations required to implement **minimap click-to-move navigation**—translating 2D screen coordinate clicks on the rotated minimap UI into 3D world pathfinding and movement commands.

---

## 1. Directory & Code Reference Map

To implement or modify ground click movement or minimap navigation, you must coordinate between the following files:

| Layer / Concern | Target File Path | Purpose |
| :--- | :--- | :--- |
| **Minimap Mouse Inputs** | [`src/source/UI/NewUI/HUD/NewUIMiniMap.cpp`](src/source/UI/NewUI/HUD/NewUIMiniMap.cpp) | Handled by `CNewUIMiniMap::UpdateMouseEvent` and the currently empty stub `CNewUIMiniMap::Check_Mouse`. |
| **Ground Clicking & Movement Loop** | [`src/source/Engine/Object/ZzzInterface.cpp`](src/source/Engine/Object/ZzzInterface.cpp) | Handled by `MoveHero()`. Runs every frame during active gameplay to poll inputs and initiate pathing. |
| **Pathfinding Engine** | [`src/source/Engine/AI/ZzzAI.cpp`](src/source/Engine/AI/ZzzAI.cpp) | Exposes `bool PathFinding2(int sx, int sy, int tx, int ty, PATH_t* a, float fDistance = 0.f, int iDefaultWall = TW_CHARACTER)`. |
| **Network Packets** | [`src/source/Engine/Object/ZzzInterface.cpp`](src/source/Engine/Object/ZzzInterface.cpp) | Exposes `void SendMove(CHARACTER* c, OBJECT* o)`, which calls `SendCharacterMove` to dispatch network movement packets. |
| **Minimap Math / Opengl Rendering** | [`src/source/Render/Textures/ZzzOpenglUtil.cpp`](src/source/Render/Textures/ZzzOpenglUtil.cpp) | Houses `RenderBitRotate` and `RenderPointRotate` which define the minimap's isometric (45° rotated) coordinate math. |

---

## 2. Deep-Dive: How Ground-Click Movement Works (`MoveHero()`)

Inside `MoveHero()`, when the player clicks on the 3D ground without selecting an NPC, item, or enemy (lines 7746–7795):
1. **Collision Check**: The terrain under the cursor is hit-tested using `CollisionDetectLineToFace`, generating a 3D intersection vector in `CollisionPosition` (in world coordinates, where `TERRAIN_SCALE = 100.f`).
2. **Coordinate Scale**: The world coordinate is scaled down by `TERRAIN_SCALE` to map grid coordinates (0 to 255):
   ```cpp
   TargetX = (BYTE)(CollisionPosition[0] / TERRAIN_SCALE);
   TargetY = (BYTE)(CollisionPosition[1] / TERRAIN_SCALE);
   ```
3. **Pathfinding Trigger**: If the clicked tile is walkable (checked via `TerrainWall[TargetY * 256 + TargetX]`), the client triggers:
   ```cpp
   PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY, &c->Path)
   ```
4. **Move Dispatch**: If a valid path is found:
   - Sets the movement state: `c->MovementType = MOVEMENT_MOVE;`
   - Dispatches packets: `SendMove(c, o);`
   - Spawns the golden rings visual feedback indicator on the terrain:
     ```cpp
     DeleteEffect(MODEL_MOVE_TARGETPOSITION_EFFECT);
     CreateEffect(MODEL_MOVE_TARGETPOSITION_EFFECT, CollisionPosition, pHeroObj->Angle, vLight, 0, pHeroObj, -1, 0, 0, 0, 0.6f);
     ```

---

## 3. Mathematical Mapping: Minimap Coordinates to Grid Tiles

The minimap image is rendered centered around the player and **rotated by 45° (`Rot = 45.f`)** to match the isometric gameplay view.
- **Reference Center**: In the standard `640x480` UI reference space, the player is drawn at the center of the viewport:
  - `PlayerCenterX = 320.0f`
  - `PlayerCenterY = 240.0f`
- **Mouse Relative Offsets**: Let `(mx, my)` be the clicked mouse position in reference space:
  ```cpp
  float dx = mx - 320.0f;
  float dy = 240.0f - my; // Flip Y because mouse coordinates start at the top, OpenGL starts at the bottom
  ```

### Rotating Back to Map Space (Inverse Rotation by -45°)
Since the map is rotated clockwise by 45°, we apply a counter-clockwise rotation of -45° to un-rotate the offset vector `(dx, dy)` back into flat texture-space offsets `(unrotated_dx, unrotated_dy)`:
$$\begin{aligned}
\text{unrotated\_dx} &= dx \cdot \cos(-45^\circ) - dy \cdot \sin(-45^\circ) = (dx + dy) \cdot 0.70710678f \\
\text{unrotated\_dy} &= dx \cdot \sin(-45^\circ) + dy \cdot \cos(-45^\circ) = (-dx + dy) \cdot 0.70710678f
\end{aligned}$$

### Scaling Back to Map Grid Coordinates (0 to 255)
The flat map texture coordinates are scaled based on the current minimap zoom level dimensions `m_Lenth[m_MiniPos]`. By default (zoom level 0), `Width = Height = 800.0f` for the 256x256 tile map.
- **Tile Scale**: $Scale_x = \frac{m\_Lenth[m\_MiniPos].x}{256.f}$, $Scale_y = \frac{m\_Lenth[m\_MiniPos].y}{256.f}$
- **Coordinate Conversion**:
  - The flat map's horizontal axis corresponds to the grid's **Y-coordinate** (`PositionY`).
  - The flat map's vertical axis corresponds to the grid's **X-coordinate** (`PositionX`).
- **Equations**:
  $$\begin{aligned}
  \text{target\_PositionY} &= \text{Hero}\rightarrow\text{PositionY} + \text{unrotated\_dx} \cdot \left(\frac{256.0f}{m\_Lenth[m\_MiniPos].x}\right) \\
  \text{target\_PositionX} &= \text{Hero}\rightarrow\text{PositionX} - \text{unrotated\_dy} \cdot \left(\frac{256.0f}{m\_Lenth[m\_MiniPos].y}\right)
  \end{aligned}$$

`RenderPointRotate` builds its local point as `(LocationY - HeroY, HeroX - LocationX)` before
applying the 45° rotation, so the inverse vertical component is subtracted from `PositionX`.

---

## 4. Implementation Draft: Minimap Click-to-Move

To implement this feature, update `CNewUIMiniMap::Check_Mouse(int mx, int my)` in `NewUIMiniMap.cpp` with the following implementation. 

### A. Coordinate Calculations & Movement Dispatch
```cpp
bool SEASON3B::CNewUIMiniMap::Check_Mouse(int mx, int my)
{
    // 1. Calculate relative offset from reference center (320, 240)
    float dx = (float)mx - 320.0f;
    float dy = 240.0f - (float)my;

    // 2. Perform inverse rotation by -45 degrees
    float unrotated_dx = (dx + dy) * 0.70710678f;
    float unrotated_dy = (-dx + dy) * 0.70710678f;

    // 3. Map to tile grid based on zoom scaling
    float zoom_width = (float)m_Lenth[m_MiniPos].x;
    float zoom_height = (float)m_Lenth[m_MiniPos].y;

    int target_Y = (int)(Hero->PositionY + unrotated_dx * (256.0f / zoom_width));
    int target_X = (int)(Hero->PositionX - unrotated_dy * (256.0f / zoom_height));

    // 4. Bound coordinates inside the 256x256 map
    if (target_X < 0) target_X = 0;
    if (target_X > 255) target_X = 255;
    if (target_Y < 0) target_Y = 0;
    if (target_Y > 255) target_Y = 255;

    // 5. Check if the tile is walkable
    WORD attrib = TerrainWall[target_Y * 256 + target_X];
    if (attrib >= TW_NOGROUND && (attrib & TW_ACTION) != TW_ACTION && (attrib & TW_HEIGHT) != TW_HEIGHT)
    {
        // Not a walkable tile (wall or out of bounds)
        return false;
    }

    // 6. Find path and command character movement
    if (PathFinding2(Hero->PositionX, Hero->PositionY, target_X, target_Y, &Hero->Path))
    {
        // Set variables for MoveHero loop compatibility
        TargetX = target_X;
        TargetY = target_Y;
        Hero->MovementType = MOVEMENT_MOVE;
        SendMove(Hero, &Hero->Object);

        // 7. Spawn visual golden rings indicator on the 3D ground
        vec3_t target_world_pos;
        target_world_pos[0] = (float)target_X * TERRAIN_SCALE + (TERRAIN_SCALE * 0.5f);
        target_world_pos[1] = (float)target_Y * TERRAIN_SCALE + (TERRAIN_SCALE * 0.5f);
        target_world_pos[2] = RequestTerrainHeight(target_world_pos[0], target_world_pos[1]);

        vec3_t vLight;
        Vector(1.f, 1.f, 0.f, vLight);
        DeleteEffect(MODEL_MOVE_TARGETPOSITION_EFFECT);
        CreateEffect(MODEL_MOVE_TARGETPOSITION_EFFECT, target_world_pos, Hero->Object.Angle, vLight, 0, &Hero->Object, -1, 0, 0, 0, 0.6f);

        return true;
    }

    return false;
}
```

### B. Input Interceptor in `UpdateMouseEvent`
Verify that `UpdateMouseEvent` in `NewUIMiniMap.cpp` blocks clicking through the map interface to the terrain below by returning `false` (blocking input propagation) when clicking successfully in bounds:
```cpp
    if (IsPress(VK_LBUTTON))
    {
        if (CheckMouseIn(0, 0, REFERENCE_WIDTH, 430))
        {
            if (Check_Mouse(MouseX, MouseY))
            {
                PlayBuffer(SOUND_CLICK01);
                return false; // Consume mouse event
            }
        }
    }
```
