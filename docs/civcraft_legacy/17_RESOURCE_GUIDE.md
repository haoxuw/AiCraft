# Resource Guide: Making CivCraft Look Better Than Minecraft Dungeons

This document catalogs open-source resources (textures, models, sounds, shaders) with permissive licenses (CC0, MIT, CC-BY) that we can use or mimic.

---

## 1. TEXTURES (PBR Block Materials)

### Top-Tier CC0 PBR Libraries

| Source | URL | License | Content |
|--------|-----|---------|---------|
| **ambientCG** | https://ambientcg.com/ | CC0 | 2000+ PBR sets (albedo, normal, roughness, AO, displacement). 1K-4K. Stone, brick, wood, dirt, grass, metal. **Primary source.** |
| **Poly Haven** | https://polyhaven.com/textures | CC0 | Photoscanned PBR sets up to 8K. Terrain, wood, stone, brick. Higher quality than ambientCG, smaller library. |
| **cgbookcase** | https://www.cgbookcase.com/textures | CC0 | 380+ PBR sets. Focus on natural materials (stone, wood, ground, leaves). |
| **TextureCan** | https://www.texturecan.com/ | CC0 | Stone, wood, metal, ground, wall PBR textures. |
| **ShareTextures** | https://www.sharetextures.com/ | CC0 | 300+ PBR sets up to 4K. Building materials, planks, stone bricks. |
| **3DTextures.me** | https://3dtextures.me/ | CC0 | Seamless PBR textures with full map sets. |

### Voxel-Specific Texture Packs

| Source | URL | License | Content |
|--------|-----|---------|---------|
| **Demon's Nation of Realism** | https://github.com/darkyboys/demon-nation-of-realism | MIT | 256x/512x/1024x PBR block textures. POM support. Designed for block-based games. **Best fit.** |
| **Null-MC/Oversized** | https://github.com/Null-MC/Oversized | CC0 | 64x-512x PBR in LabPBR format. Parallax occlusion mapping. |
| **Pixel Perfection** | https://github.com/minetest-texture-packs/Pixel-Perfection | CC-BY-SA-4.0 | 16x stylized pixel art for Luanti. Complete block/item/UI coverage. |

### PBR Map Generation Tools

| Tool | URL | License | Purpose |
|------|-----|---------|---------|
| **Material Maker** | https://www.materialmaker.org/ | MIT | Node-based procedural PBR texture generator. 250 built-in nodes. |
| **PixelGraph** | https://github.com/Null-MC/PixelGraph | Check repo | Generate normal/AO/roughness from height maps. LabPBR encoding. |
| **Laigter** | https://azagaya.itch.io/laigter | Open source | Generate normal, specular, AO from 2D textures. |
| **AwesomeBump** | https://github.com/kmkolasinski/AwesomeBump | Open source | One-click PBR map generation from albedo. |

### Curated Lists

- **madjin/awesome-cc0**: https://github.com/madjin/awesome-cc0 -- Master list of CC0 assets
- **Kimbatt/cc0-textures**: https://github.com/Kimbatt/cc0-textures -- Bulk mirror of CC0 PBR textures
- **Free PBR Sites Gist**: https://gist.github.com/mauricesvay/1330cc530f6ab2ef33eb6a5ea56ef5bd -- 19+ PBR texture sites

---

## 2. 3D MODELS & VISUAL ASSETS

### Voxel Models (CC0)

| Source | URL | License | Content |
|--------|-----|---------|---------|
| **Kenney Voxel Pack** | https://kenney.nl/assets/voxel-pack | CC0 | ~170 voxel models (OBJ/glTF). Characters, animals, trees, furniture. |
| **Kenney Mini Dungeon** | https://kenney.nl/assets/mini-dungeon | CC0 | Dungeon-themed voxel models. Walls, doors, chests, torches, enemies. |
| **mikelovesrobots/mmmm** | https://github.com/mikelovesrobots/mmmm | CC-BY 4.0 | Hundreds of MagicaVoxel .vox models. Food, furniture, weapons. |

### Low-Poly Animated Models (CC0) -- Key for Beating Minecraft Dungeons

| Source | URL | License | Content |
|--------|-----|---------|---------|
| **Quaternius** | https://quaternius.com/packs.html | CC0 | Animated characters, weapons, monsters, animals, nature. FBX/OBJ. 300-2000 tris. Walk/attack/idle/death animations. **Top pick for mobs.** |
| **KayKit (Kay Lousberg)** | https://kaylousberg.itch.io/ | CC0 | Dungeon Pack, Character Pack, Adventurers, Skeletons. Animated stylized models. |
| **Kenney Nature/Animal/Weapon Kits** | https://kenney.nl/assets | CC0 | Low-poly trees, rocks, animals, swords, food, furniture. 50-500 polys. |

### Particle & UI Assets

| Source | URL | License | Content |
|--------|-----|---------|---------|
| **Kenney Particle Pack** | https://kenney.nl/assets/particle-pack | CC0 | 52 particle sprites (circles, stars, smoke, sparks, flares). PNG. |
| **Kenney UI Pack RPG** | https://kenney.nl/assets/ui-pack-rpg-expansion | CC0 | Inventory grids, item frames, equipment slots, stat bars, buttons. |
| **Kenney Game Icons** | https://kenney.nl/assets/game-icons | CC0 | 250+ game icons (weapons, potions, armor, tools, crafting). |
| **Game-Icons.net** | https://game-icons.net | CC-BY 3.0 | 4000+ SVG game icons. Skills, items, status effects. |
| **Effekseer** | https://github.com/effekseer/Effekseer | MIT | Particle editor + sample effect textures (fire, explosions, magic). |

### Skyboxes

| Source | URL | License | Content |
|--------|-----|---------|---------|
| **Poly Haven HDRIs** | https://polyhaven.com/hdris | CC0 | Hundreds of HDR environment maps. Outdoor skies, 1K-16K. Convertible to cubemaps. |

### Fonts

| Source | URL | License | Content |
|--------|-----|---------|---------|
| **Kenney Fonts** | https://kenney.nl/assets/kenney-fonts | CC0 | Game-ready TTF fonts (blocky, pixel, stylized). |
| **Press Start 2P** | https://fonts.google.com/specimen/Press+Start+2P | OFL | Classic pixel font. |

### One-Stop Shop

- **Kenney Full Asset Pack** (GitHub mirror): https://github.com/iwenzhou/kenney -- 60,000+ CC0 assets (all categories)

---

## 3. SOUND EFFECTS & MUSIC

### Sound Effect Libraries

| Source | URL | License | Best For |
|--------|-----|---------|----------|
| **Kenney Audio Packs** | https://kenney.nl/assets/category:Audio | CC0 | Impact (130 sfx), UI (50), RPG (50), Interface (100). **Start here.** |
| **Freesound.org** | https://freesound.org | Mixed (filter CC0) | 600K+ sounds. Search specific needs (pig oink, chicken cluck, etc). |
| **SONNISS GDC Bundle** | https://gdc.sonniss.com/ | Royalty-free | Professional-grade. 200+ GB across yearly bundles (2014-2026). |
| **Pixabay SFX** | https://pixabay.com/sound-effects/ | Pixabay License | 110K+ sounds. Free commercial use, no attribution. |
| **Mixkit** | https://mixkit.co/free-sound-effects/ | Mixkit License | Curated high-quality game/explosion/footstep/nature sounds. WAV. |

### OpenGameArt CC0 Sound Packs (by rubberduck) -- Best Value

| Pack | URL | Count | Content |
|------|-----|-------|---------|
| **75 Breaking/Falling/Hit** | https://opengameart.org/content/75-cc0-breaking-falling-hit-sfx | 75 | Wood breaking, metal impacts, glass, stone cracking |
| **100 CC0 SFX** | https://opengameart.org/content/100-cc0-sfx | 100 | Bells, doors, explosions, glass, metal, splashes, tools |
| **100 CC0 SFX #2** | https://opengameart.org/content/100-cc0-sfx-2 | 100 | Wind, footsteps, ambient loops, thunder, water |
| **100 Metal & Wood** | https://opengameart.org/content/100-cc0-metal-and-wood-sfx | 100 | Mining tools, crafting, block breaking |
| **80 RPG SFX** | https://opengameart.org/content/80-cc0-rpg-sfx | 80 | Blade, creature, item, spell, coin sounds |
| **80 Creature SFX** | https://opengameart.org/content/80-cc0-creature-sfx | 80 | Monster, animal, grunt, roar, eat sounds |
| **40 Water/Splash/Slime** | https://opengameart.org/content/40-cc0-water-splash-slime-sfx | 40 | Water, rain loops, slime, splash |
| **51 UI SFX** | https://opengameart.org/content/51-ui-sound-effects-buttons-switches-and-clicks | 51 | Clicks, rollovers, switches |

### Footstep Sounds (Material-Specific)

| Source | URL | License | Surfaces |
|--------|-----|---------|----------|
| **Footsteps on Surfaces** | https://opengameart.org/content/footsteps-on-different-surfaces | CC-BY 3.0 | Concrete, grass, gravel, metal, wood, water |
| **Steps on Materials** | https://opengameart.org/content/different-steps-on-wood-stone-leaves-gravel-and-mud | CC0 | Wood, stone, leaves, gravel, mud |
| **Walking Steps** | https://opengameart.org/content/foot-walking-step-sounds-on-stone-water-snow-wood-and-dirt | CC0 | Stone, water, snow, wood, dirt, grass |

### Music

| Source | URL | License | Content |
|--------|-----|---------|---------|
| **OGA CC0 Music** | https://opengameart.org/content/cc0-music-0 | CC0 | 500+ tracks: orchestral, ambient, battle, exploration, medieval |
| **alkakrab Medieval** | https://alkakrab.itch.io/free-fantasy-medieval-ambient-music-pack | Free commercial | 10 ambient medieval tracks + loops. 788 MB. |
| **alkakrab Exploration** | https://alkakrab.itch.io/free-fantasy-exploration-ambient-music-pack | Free commercial | 8 exploration tracks + loops |
| **Incompetech Medieval** | https://incompetech.com/music/packs.html | CC-BY 4.0 | Kevin MacLeod game bundles. High-fantasy style. |
| **itch.io CC0 Music** | https://itch.io/game-assets/assets-cc0/tag-music | CC0 | Fantasy, battle, RPG loops |
| **Pixabay Music** | https://pixabay.com/music/ | Pixabay License | Dungeon, medieval, ambient, fantasy orchestral |

### Sound Mapping for CivCraft

| Category | Primary Source | Backup |
|----------|---------------|--------|
| Block dig/place | rubberduck 75 Breaking + 100 Metal&Wood | Kenney Impact |
| Footsteps | OGA Walking Steps (CC0) | Freesound CC0 |
| Ambient nature | SONNISS GDC | Freesound forest loops |
| UI sounds | Kenney UI Audio + Interface | OGA 51 UI SFX |
| Explosions | OGA 100 CC0 SFX | Mixkit |
| Mob sounds | OGA 80 Creature SFX + 80 RPG | Freesound/Pixabay |
| Water | OGA 40 Water/Splash/Slime | Freesound |
| Item pickup | Kenney RPG Audio | OGA 12 Coin SFX |
| Music (explore) | alkakrab Medieval + OGA CC0 | itch.io CC0 |
| Music (combat) | itch.io Epic Battle BGM | Incompetech |
| Music (dungeon) | OGA Dungeon Ambience | Pixabay |

---

## 4. RENDERING TECHNIQUES & SHADER REFERENCES

### Production-Grade Open-Source Engines (Study Architecture)

| Engine | URL | License | API | Key Techniques |
|--------|-----|---------|-----|----------------|
| **Wicked Engine** | https://github.com/turanszkij/WickedEngine | MIT | DX12/Vulkan | CSM, VXGI, SSAO, bloom, HDR, volumetric fog, god rays, SSR, DoF, ocean FFT. **Gold standard.** |
| **Veloren** | https://gitlab.com/veloren/veloren | GPL-3.0 | wgpu/GLSL | Dynamic shadows, volumetric fog, water caustics, 33 experimental shaders, FXAA. **GLSL shaders directly usable.** |
| **Terasology** | https://github.com/MovingBlocks/Terasology | Apache 2.0 | OpenGL | Modular deferred/forward pipeline. Best license for copying. |

### C++/OpenGL Voxel Rendering (Closest to Our Stack)

| Repo | URL | License | Key Techniques |
|------|-----|---------|----------------|
| **Gengine** | https://github.com/JuanDiegoMontoya/Gengine | Check | HDR, CoD:AW bloom, 4-channel flood-fill voxel lighting, FXAA, 10M GPU particles. **Most similar to CivCraft.** |
| **OpenGL-VXGI-Engine** | https://github.com/Hanlin-Zhou/OpenGL-VXGI-Engine | Check | Deferred pipeline, voxel cone tracing GI, PCSS, SSAO. Clean readable code. |
| **St0wy's Deferred PBR** | https://github.com/St0wy/opengl-scene | Check | Deferred PBR, CSM, SSAO, ACES tonemapping, bloom. Uses glad+glm like us. [Blog](https://blog.stowy.ch/posts/how-i-implemented-a-deferred-pbr-renderer-in-opengl/) |
| **VCTRenderer** | https://github.com/jose-villegas/VCTRenderer | Check | Voxel cone tracing GI, conservative voxelization, soft shadows, emissive materials. We skip voxelization since we ARE a voxel engine. |
| **CUBE** | https://github.com/andrewd440/CUBE | Check | Deferred voxel rendering, AO, infinite world gen. |

### Specific Effect Implementations (MIT Licensed)

| Effect | URL | License | Notes |
|--------|-----|---------|-------|
| **Cascaded Shadow Maps** | https://github.com/diharaw/cascaded-shadow-maps | MIT | PSSM, stable cascades, PCF. Drop-in reference. |
| **Volumetric Fog** | https://github.com/diharaw/volumetric-fog | MIT | Frustum-aligned 3D texture + compute shaders. Frostbite/UE approach. |
| **GPU Particle System** | https://github.com/diharaw/gpu-particle-system | MIT | Compute shader particles, indirect draw. |
| **GPU Particles + Fluid** | https://github.com/Jax922/gpu-particle-system | Check | Fire/fluid/collision sim. C++20/GL4.6. |
| **Screen-Space God Rays** | https://github.com/math-araujo/screen-space-godrays | Check | Post-process radial sampling. Cheap, high impact. |
| **GLSL God Rays** | https://github.com/Erkaman/glsl-godrays | Check | Minimal standalone GLSL module. |
| **SSR** | https://github.com/DanonOfficial/ScreenSpaceReflection | Check | Depth-buffer ray march for water reflections. |
| **Water Rendering** | https://github.com/teodorplop/OpenGL-Water | Check | FBO reflection/refraction compositing. |
| **Shadow Mapping Examples** | https://github.com/Flix01/Tiny-OpenGL-Shadow-Mapping-Examples | Check | Single-file C shadow mapping examples. |

### Minecraft Voxel Shader Packs (GLSL, study techniques)

| Shader | URL | License | Key Techniques |
|--------|-----|---------|----------------|
| **Rethinking Voxels** | https://github.com/gri573/rethinking-voxels | Attribution req. | Colored flood-fill voxel lighting, ray-traced occlusion, volumetric scattering. |
| **Luanti Shaders 2.0** | https://github.com/DragonWrangler1/minetest-shaders-2.0_5.8-edition | Check | Dynamic shadows, god rays, liquid reflections, bloom for Luanti. |

### Learning Resources

| Resource | URL | Key Topics |
|----------|-----|------------|
| **3D Game Shaders For Beginners** | https://github.com/lettier/3d-game-shaders-for-beginners | 34 techniques: SSAO, deferred, bloom, SSR, normal mapping, DoF, fog, cel shading |
| **LearnOpenGL** | https://learnopengl.com | [Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping), [CSM](https://learnopengl.com/Guest-Articles/2021/CSM), [SSAO](https://learnopengl.com/Advanced-Lighting/SSAO), [HDR](https://learnopengl.com/Advanced-Lighting/HDR), [Bloom](https://learnopengl.com/Advanced-Lighting/Bloom), [PBR](https://learnopengl.com/PBR/Theory), [Deferred](https://learnopengl.com/Advanced-Lighting/Deferred-Shading) |
| **Open-Shaders Collection** | https://github.com/repalash/Open-Shaders | Aggregated shaders from Unity, Unreal, Godot, Filament (Google) |

---

## 5. IMPLEMENTATION PRIORITY

To surpass Minecraft Dungeons visuals, implement in this order (max visual impact per effort):

### Phase 1: Foundations (Biggest Bang for Buck)
1. **Shadow Mapping** -- Directional sun shadows, then cascaded. Single biggest upgrade.
   - Ref: LearnOpenGL CSM + `diharaw/cascaded-shadow-maps` (MIT)
2. **HDR + Tonemapping** -- Switch to `GL_RGBA16F` FBOs, ACES tonemapping. Enables everything else.
   - Ref: Gengine, St0wy's blog
3. **Bloom** -- Physically-based (progressive downsample/upsample, not naive Gaussian).
   - Ref: LearnOpenGL Phys-Based Bloom, Gengine's CoD:AW approach

### Phase 2: Atmosphere
4. **SSAO** -- Screen-space ambient occlusion. Adds depth and grounding.
   - Ref: `lettier/3d-game-shaders-for-beginners`, St0wy
5. **God Rays** -- Post-process volumetric light scattering from sun. Cheap and stunning.
   - Ref: `math-araujo/screen-space-godrays`, `Erkaman/glsl-godrays`
6. **Volumetric Fog** -- Frustum-aligned voxel grid. Dungeon atmosphere.
   - Ref: `diharaw/volumetric-fog` (MIT)

### Phase 3: Water & Reflections
7. **Water Reflections/Refraction** -- Screen-space for performance.
   - Ref: Veloren water shaders, `teodorplop/OpenGL-Water`

### Phase 4: Advanced
8. **Deferred Pipeline** -- Enables multi-light, PBR, SSR.
   - Ref: OpenGL-VXGI-Engine, St0wy's blog
9. **PBR Materials** -- Metallic/roughness workflow with texture packs above.
   - Ref: LearnOpenGL PBR, Filament shaders
10. **Voxel GI (Cone Tracing)** -- Indirect lighting with color bleeding. We skip voxelization (we ARE voxels).
    - Ref: VCTRenderer

### What Made Minecraft Dungeons Look Good
- Strong directional lighting with warm/cool contrast
- Heavy bloom on light sources and magic effects
- Volumetric fog in dungeons for atmosphere
- Stylized PBR (not photorealistic, but proper metalness/roughness)
- Rich particle effects for combat feedback
- Baked + dynamic lighting hybrid

Phases 1-3 above (6 techniques) get us to Minecraft Dungeons quality. Phases 4+ surpass it.

---

## 6. LICENSE SUMMARY

| License | Can Use Commercially | Attribution Required | Share-Alike | Sources |
|---------|---------------------|---------------------|-------------|---------|
| **CC0** | Yes | No | No | ambientCG, Poly Haven, Kenney, Quaternius, KayKit, OGA rubberduck |
| **MIT** | Yes | Yes (in source) | No | Wicked Engine, Material Maker, Demon's Nation, diharaw repos |
| **CC-BY 3.0/4.0** | Yes | Yes (visible credit) | No | Game-Icons.net, Incompetech, Poly Pizza |
| **CC-BY-SA** | Yes | Yes | Yes (derivatives same license) | Pixel Perfection |
| **Apache 2.0** | Yes | Yes (in source) | No | Terasology |
| **GPL-3.0** | Yes (but viral) | Yes | Yes (entire work) | Veloren (study only, don't copy) |
| **Pixabay License** | Yes | No | No (can't resell standalone) | Pixabay sounds/music |

**Safe strategy**: Prioritize CC0 and MIT resources. Use GPL only for studying techniques (rewrite, don't copy).
