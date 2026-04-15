import * as THREE from 'three';
import { Text } from 'troika-three-text';

// Modern UI primitives for CellCraft web.
//
// Palette guidance: the playfield is cream paper (#F2ECD8-ish). UI elements
// prefer deep chalky hues (#2B2B2B, #3B4A52) with subtle neon accents
// (#58E6B7, #FF6E8A, #FFC857). Everything here returns plain THREE.Object3D
// trees so callers can position them anywhere in a scene (e.g. an
// orthographic HUD overlay). All ShaderMaterials are GLSL3.

// ---- Palette ---------------------------------------------------------------
export const UI_PALETTE = {
  paper: new THREE.Color(0xf2ecd8),
  chalk: new THREE.Color(0x2b2b2b),
  chalkSoft: new THREE.Color(0x3b4a52),
  neonCyan: new THREE.Color(0x58e6b7),
  neonPink: new THREE.Color(0xff6e8a),
  neonAmber: new THREE.Color(0xffc857),
  hpRed: new THREE.Color(0xd04158),
  xpBlue: new THREE.Color(0x4aa3f0)
};

// ---- Text ------------------------------------------------------------------
export interface TextOpts {
  size?: number;
  color?: THREE.ColorRepresentation;
  glow?: boolean | THREE.ColorRepresentation;
  weight?: 'regular' | 'bold';
  anchorX?: 'left' | 'center' | 'right';
  anchorY?: 'top' | 'middle' | 'bottom';
}

export function makeText(content: string, opts: TextOpts = {}): THREE.Object3D {
  const t = new Text();
  t.text = content;
  t.fontSize = opts.size ?? 24;
  t.color = new THREE.Color(opts.color ?? UI_PALETTE.chalk).getHex();
  t.anchorX = opts.anchorX ?? 'center';
  t.anchorY = opts.anchorY ?? 'middle';
  // troika supports synthetic weight via outlineWidth; we also use
  // outline for neon glow so bloom picks it up.
  if (opts.weight === 'bold') {
    t.outlineWidth = Math.max(0.5, (opts.size ?? 24) * 0.04);
    t.outlineColor = new THREE.Color(opts.color ?? UI_PALETTE.chalk).getHex();
    t.outlineOpacity = 1.0;
  }
  if (opts.glow) {
    const glowColor =
      typeof opts.glow === 'boolean'
        ? new THREE.Color(UI_PALETTE.neonCyan).getHex()
        : new THREE.Color(opts.glow).getHex();
    t.outlineWidth = Math.max(1.2, (opts.size ?? 24) * 0.09);
    t.outlineColor = glowColor;
    t.outlineOpacity = 0.85;
    t.outlineBlur = Math.max(2.0, (opts.size ?? 24) * 0.25);
  }
  // Disposed by caller via a walk; troika Text has its own sync.
  (t as unknown as { sync: () => void }).sync();
  return t as unknown as THREE.Object3D;
}

// ---- Glass panel (rounded-rect w/ inner glow + border) ---------------------
export interface GlassPanelOpts {
  radius?: number;
  tint?: THREE.ColorRepresentation;
  alpha?: number;
  borderColor?: THREE.ColorRepresentation;
}

const GLASS_FRAG = /* glsl */ `
  precision highp float;
  in vec2 v_uv;
  out vec4 pc_fragColor;

  uniform vec2  u_size;     // panel size in local units
  uniform float u_radius;
  uniform vec3  u_tint;
  uniform float u_alpha;
  uniform vec3  u_border;

  // Signed distance to rounded rect.
  float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
  }

  float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
  }

  void main() {
    vec2 p = (v_uv - 0.5) * u_size;
    float d = sdRoundedBox(p, u_size * 0.5, u_radius);
    float aa = 1.0;

    if (d > 0.0) { discard; }

    // Inner glow: brighten near edges a little.
    float edge = smoothstep(-u_radius * 0.6, 0.0, d);
    // Subtle chalk-noise tint.
    float n = hash21(floor(p * 0.6)) * 0.06;
    vec3 col = u_tint + n - 0.03;
    col = mix(col, col * 1.12, edge);

    // Thin border stroke (about 1.2 local units wide).
    float border = smoothstep(-1.6, -0.2, d) - smoothstep(-0.2, 0.8, d);
    border = clamp(border, 0.0, 1.0);
    col = mix(col, u_border, border * 0.85);

    // Antialias at the outer edge.
    float fade = 1.0 - smoothstep(-1.2, 0.5, d);
    pc_fragColor = vec4(col, u_alpha * fade * aa);
  }
`;

const GLASS_VERT = /* glsl */ `
  out vec2 v_uv;
  void main() {
    v_uv = uv;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  }
`;

export function makeGlassPanel(w: number, h: number, opts: GlassPanelOpts = {}): THREE.Mesh {
  const geom = new THREE.PlaneGeometry(w, h);
  const mat = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    vertexShader: GLASS_VERT,
    fragmentShader: GLASS_FRAG,
    uniforms: {
      u_size: { value: new THREE.Vector2(w, h) },
      u_radius: { value: opts.radius ?? Math.min(w, h) * 0.12 },
      u_tint: { value: new THREE.Color(opts.tint ?? 0x1a2026) },
      u_alpha: { value: opts.alpha ?? 0.82 },
      u_border: { value: new THREE.Color(opts.borderColor ?? UI_PALETTE.chalkSoft) }
    },
    transparent: true,
    depthTest: false,
    depthWrite: false
  });
  const mesh = new THREE.Mesh(geom, mat);
  return mesh;
}

// ---- Stat bar --------------------------------------------------------------
export interface StatBarOpts {
  color?: THREE.ColorRepresentation;
  bg?: THREE.ColorRepresentation;
  radius?: number;
}

const STATBAR_FRAG = /* glsl */ `
  precision highp float;
  in vec2 v_uv;
  out vec4 pc_fragColor;

  uniform vec2  u_size;
  uniform float u_radius;
  uniform vec3  u_color;
  uniform float u_value;   // 0..1
  uniform float u_time;
  uniform float u_is_fill; // 1.0 = fill layer, 0.0 = bg

  float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
  }

  void main() {
    vec2 p = (v_uv - 0.5) * u_size;
    float d = sdRoundedBox(p, u_size * 0.5, u_radius);
    if (d > 0.0) { discard; }

    // Fill mask: clip x to [-W/2, -W/2 + W*value].
    float xnorm = v_uv.x; // 0..1
    float fillMask = 1.0;
    if (u_is_fill > 0.5) {
      fillMask = step(xnorm, u_value);
      if (fillMask < 0.5) discard;
    }

    vec3 col = u_color;
    if (u_is_fill > 0.5) {
      // Animated sheen: a bright diagonal band sweeping across.
      float sheen = sin((xnorm * 6.0 - u_time * 2.0) * 3.14159);
      sheen = smoothstep(0.6, 1.0, sheen);
      col += vec3(1.0) * sheen * 0.18;
      // Top highlight: brighter upper edge.
      float up = smoothstep(0.2, 1.0, v_uv.y);
      col = mix(col, col * 1.2, up * 0.35);
    }

    float fade = 1.0 - smoothstep(-1.0, 0.5, d);
    pc_fragColor = vec4(col, fade);
  }
`;

export interface StatBarHandle {
  group: THREE.Group;
  setValue(t: number): void;
  setTime(t: number): void;
}

export function makeStatBar(w: number, h: number, opts: StatBarOpts = {}): StatBarHandle {
  const radius = opts.radius ?? h * 0.5;
  const group = new THREE.Group();

  const mkMat = (isFill: boolean, color: THREE.Color) =>
    new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: GLASS_VERT,
      fragmentShader: STATBAR_FRAG,
      uniforms: {
        u_size: { value: new THREE.Vector2(w, h) },
        u_radius: { value: radius },
        u_color: { value: color.clone() },
        u_value: { value: 1.0 },
        u_time: { value: 0.0 },
        u_is_fill: { value: isFill ? 1.0 : 0.0 }
      },
      transparent: true,
      depthTest: false,
      depthWrite: false
    });

  const bgMat = mkMat(false, new THREE.Color(opts.bg ?? 0x1a2026));
  const fillMat = mkMat(true, new THREE.Color(opts.color ?? UI_PALETTE.hpRed));

  const geom = new THREE.PlaneGeometry(w, h);
  const bg = new THREE.Mesh(geom, bgMat);
  const fill = new THREE.Mesh(geom.clone(), fillMat);
  fill.position.z = 0.01;
  group.add(bg);
  group.add(fill);

  return {
    group,
    setValue(t) {
      fillMat.uniforms.u_value.value = Math.max(0, Math.min(1, t));
    },
    setTime(t) {
      fillMat.uniforms.u_time.value = t;
    }
  };
}

// ---- Ring progress (SDF arc) ----------------------------------------------
export interface RingProgressOpts {
  color?: THREE.ColorRepresentation;
  bg?: THREE.ColorRepresentation;
}

const RING_FRAG = /* glsl */ `
  precision highp float;
  in vec2 v_uv;
  out vec4 pc_fragColor;

  uniform float u_radius;
  uniform float u_thickness;
  uniform float u_value;    // 0..1
  uniform vec3  u_color;
  uniform vec3  u_bg;
  uniform float u_size;     // plane size (square)

  void main() {
    vec2 p = (v_uv - 0.5) * u_size;
    float d = length(p);
    float ring = abs(d - u_radius) - u_thickness * 0.5;
    if (ring > 0.0) discard;

    // Angle: 0 at top, clockwise in [0, 1).
    float ang = atan(p.x, p.y);        // [-pi, pi], 0 at +y
    float t = ang / 6.2831853 + 0.5;   // 0..1 starting at bottom? fix:
    // Re-map so 0 is at top going clockwise.
    t = fract(0.5 - ang / 6.2831853);

    float on = step(t, u_value);
    vec3 col = mix(u_bg, u_color, on);

    float fade = 1.0 - smoothstep(-1.0, 0.5, ring);
    pc_fragColor = vec4(col, fade);
  }
`;

export interface RingProgressHandle {
  group: THREE.Group;
  setValue(t: number): void;
}

export function makeRingProgress(
  radius: number,
  thickness: number,
  opts: RingProgressOpts = {}
): RingProgressHandle {
  const size = (radius + thickness) * 2.2;
  const geom = new THREE.PlaneGeometry(size, size);
  const mat = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    vertexShader: GLASS_VERT,
    fragmentShader: RING_FRAG,
    uniforms: {
      u_radius: { value: radius },
      u_thickness: { value: thickness },
      u_value: { value: 1.0 },
      u_color: { value: new THREE.Color(opts.color ?? UI_PALETTE.xpBlue) },
      u_bg: { value: new THREE.Color(opts.bg ?? 0x1a2026) },
      u_size: { value: size }
    },
    transparent: true,
    depthTest: false,
    depthWrite: false
  });
  const mesh = new THREE.Mesh(geom, mat);
  const group = new THREE.Group();
  group.add(mesh);
  return {
    group,
    setValue(t) {
      mat.uniforms.u_value.value = Math.max(0, Math.min(1, t));
    }
  };
}

// ---- Pill badge ------------------------------------------------------------
export interface PillBadgeOpts {
  color?: THREE.ColorRepresentation;
  textColor?: THREE.ColorRepresentation;
  size?: number;
}

export function makePillBadge(label: string, opts: PillBadgeOpts = {}): THREE.Group {
  const size = opts.size ?? 18;
  const approxW = label.length * size * 0.6 + size * 1.4;
  const h = size * 1.8;
  const group = new THREE.Group();
  const panel = makeGlassPanel(approxW, h, {
    radius: h * 0.5,
    tint: opts.color ?? UI_PALETTE.neonCyan,
    alpha: 0.95,
    borderColor: UI_PALETTE.chalk
  });
  group.add(panel);
  const t = makeText(label, {
    size,
    color: opts.textColor ?? UI_PALETTE.chalk,
    weight: 'bold',
    anchorX: 'center',
    anchorY: 'middle'
  });
  t.position.z = 0.02;
  group.add(t);
  return group;
}

// ---- Demo HUD --------------------------------------------------------------
// A quick composite HUD (HP bar + tier pill + XP ring + label text). Meant
// for eyeballing, not production. Callers position the group however they
// want; internally laid out around the origin of an orthographic camera.
export interface DemoHudHandle {
  group: THREE.Group;
  setHp(t: number): void;
  setXp(t: number): void;
  setTier(label: string): void;
  setTime(t: number): void;
}

export function makeDemoHud(): DemoHudHandle {
  const group = new THREE.Group();

  // HP bar near bottom-left of the group.
  const hp = makeStatBar(240, 22, { color: UI_PALETTE.hpRed });
  hp.group.position.set(0, 0, 0);
  group.add(hp.group);

  const hpLabel = makeText('HP', {
    size: 16,
    color: UI_PALETTE.paper,
    anchorX: 'left',
    anchorY: 'middle',
    weight: 'bold'
  });
  hpLabel.position.set(-130, 0, 0.1);
  group.add(hpLabel);

  // Tier pill to the right of the HP bar.
  const tierGroup = new THREE.Group();
  let currentTierPill = makePillBadge('TIER 1', { color: UI_PALETTE.neonAmber });
  tierGroup.add(currentTierPill);
  tierGroup.position.set(170, 0, 0);
  group.add(tierGroup);

  // XP ring above the HP bar.
  const xp = makeRingProgress(28, 6, { color: UI_PALETTE.neonCyan });
  xp.group.position.set(-150, 50, 0);
  group.add(xp.group);

  return {
    group,
    setHp(t) {
      hp.setValue(t);
    },
    setXp(t) {
      xp.setValue(t);
    },
    setTier(label) {
      // Rebuild the pill (simplest; labels change rarely).
      tierGroup.remove(currentTierPill);
      currentTierPill = makePillBadge(label, { color: UI_PALETTE.neonAmber });
      tierGroup.add(currentTierPill);
    },
    setTime(t) {
      hp.setTime(t);
    }
  };
}
