# Particle and smear layers

Two kinds of layer make motion for you instead of storing pixels you drew: **particle layers** (fire, smoke, sparks, snow…) and **Smear groups** (a swing, a bounce, a throw of your own art). Both are *live*: you change settings and the animation redraws. When you're happy, you can **bake** them into ordinary layers.

## Particle layers

A particle layer is an emitter that throws out little particles, frame after frame.

### Quick Effects: the fast way

**Effects → Quick Effects** adds a ready-made effect in one step: **Fire**, **Smoke**, **Sparks**, **Snow**, **Snow Pile**, **Rain**, **Fireworks**, and **Campfire** (a flame with its smoke as two layers in a group). Effects scale to your sprite's size, and Smalto adds frames if the effect needs them to play in full (the effect panel has an **Add frames** button when it needs more). Your own saved presets are listed after a divider.

### Making your own

**Layer → New Particle Layer** makes a plain emitter. The rail swaps to its own tools:

- **Emitter** (the Move tool): drag the emitter's place.
- **Source** (<kbd>Shift</kbd> + <kbd>S</kbd>): drag out the shape particles are born in, a line, a rectangle or an oval (<kbd>Shift</kbd>: square, circle, or 15° steps; <kbd>Alt</kbd>: from the middle).
- **Field** (<kbd>Shift</kbd> + <kbd>F</kbd>): click to place a field of the kind chosen in the options strip.
- **Eyedropper** and **Hand**.

The **Particles panel** holds the settings, in folding sections:

| Section | What it controls |
|---|---|
| **Emission** | How many particles, over which frames, whether the effect **loops** (the animation tiles), a **head start**, and the **seed** (a different number is a different random spread; the shuffle button at the top picks one) |
| **Source** | The shape particles are born in (line, area, oval), its size and angle, and **Fill the area** |
| **Motion** | **Direction** (0 is right, 90 down), **Spread** (the cone's width), **Speed** and how much it varies, **Slow down** (drag) and **Gravity** |
| **Fields** | Places that act on particles: **Attractor** (pulls or pushes), **Vortex** (swirls), **Wind** (a push everywhere, with gusts), **Floor** (a line particles bounce off, stay on or vanish on, with **Pile up** like snow) and **Collider** (a line of any shape particles can't cross) |
| **Life** | How many frames a particle lives (and how much that varies), how its **size** and **opacity** change over its life with an **easing**, its **fade style**, and its **colors** |
| **Look** | The particle's **shape** (you can use your current custom brush), **rotation** (angle, spin, snapping to steps for crisp pixel art) and **Face the motion** |
| **Sub-particles** | Particles that spawn more particles |

The **preset** menu at the top starts you over from a built-in preset (it replaces the settings, not where the emitter is) and **Save as preset…** keeps yours. A section's reset button returns it to the built-in values, and a dot marks a value that differs from them.

Most handles are also on the canvas. **F9** (or the eye button at the top of the panel) cycles the particle controls on the canvas between shown, faded and hidden.

## Smear groups

A **Smear group** moves and distorts ordinary art over time, with motion trails.

1. Draw your art on a layer (pointing right, like a sword drawn upright or a ball).
2. Select the layer and choose **Layer → Smear** (or right-click → **Smear**). It's wrapped in a group.
3. Use the **Pose** tool to place the art on a frame: drag inside its box to move, the round handle to turn it about the pivot (<kbd>Shift</kbd>: 15° steps), the square handles to scale (<kbd>Shift</kbd> keeps proportions; <kbd>Alt</kbd> on an edge handle skews), the ring to move the pivot. Each change makes a **key** for that frame, shown as a diamond on the group's timeline row. To see the art without the handles over it, the eye button at the top of the Smear panel (or **F9**) fades or hides them, as for particles.
4. Smalto fills in the frames between keys.

The panel shows the frame's pose as numbers (position, turn, scale, skew) and the group's settings:

- **Keys:** the list of keys; **Remove** or the **×** deletes one. **Smooth path** makes the move between keys follow a curve through their places, and **Face the path** turns the art along the way it goes. Right-click a diamond for its **easing** (Linear, In, Out, In-out, or a custom curve) and **Copy / Paste Pose**.
- **Smear:** how far back the trail reaches, how much it narrows and fades toward its tail, and an optional one-pixel **outline**.
- **Presets:** ready-made motions (Swing, Spin, Dash, Pop, Slash, Thrust, Throw, Bounce, Shake, Wobble) with a gallery that previews each; click to preview, double-click or **Apply** to use. **Save as preset…** keeps your own motion for art of any size.

To paint the art itself, pick a layer inside the group.

## Baking

Other programs can't run Smalto's particles or smears. **Layer → Bake Layer** replaces a live layer with an ordinary image layer holding what it showed on every frame (one undo step); **Bake to New Layer** keeps the live layer and adds a baked copy above it. Do this before sharing a file with other tools.
