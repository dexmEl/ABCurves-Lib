# Pixels, common angular counts and device counts

The planner operates in a consistent angular coordinate frame: X right, Y up. A common angular count is exactly `0.0003509487083280618` radians. This is a geometry unit, not an automatic mapping to a mouse/HID count. The application calibrates the camera and device; the library does not detect targets, read physical mouse position, or send calibration movements.

## Input: image error to angular target error

Use the **actual game viewport dimensions and its FOV**, with the target measured in that full-frame coordinate system. A cropped detection region does not acquire the full game's FOV: its width/height must not automatically replace the full viewport dimensions in the projection formulas. Undo any detector resizing, padding or letterboxing before mapping its coordinates back to the game viewport.

For example, a 640×640 ROI in a 2560×1440 game viewport needs its top-left offset as well as the detected ROI-local point:

```text
full_width = 2560
full_height = 1440

full_x = roi_x + roi_left
full_y = roi_y + roi_top

dx = full_x - (full_width / 2)
dy = (full_height / 2) - full_y
```

Here ROI-local image Y grows downward; `dy` is positive upward. Use `width = full_width` and `height = full_height` with the full viewport FOV below. A target at the center of a centered ROI has zero error, regardless of the ROI's dimensions. For a non-centered aiming reference, use its calibrated full-frame point instead of assuming screen center; see the ray/reference discussion below.

For a centered pinhole/perspective projection:

```python
from math import atan, tan, radians

k = 0.0003509487083280618
width, height = 2560, 1440  # Full game viewport, not the 640x640 ROI.
hfov = radians(90)
vfov = 2 * atan((height / width) * tan(hfov / 2))
dx, dy = 100, 20  # Pixels; right/up from optical center.
angle_x = atan((2 * dx / width) * tan(hfov / 2))
angle_y = atan((2 * dy / height) * tan(vfov / 2))
target_error_counts = (angle_x / k, angle_y / k)
```

These formulas compute **independent horizontal and vertical angular components**. They do not necessarily reproduce the simultaneous yaw/pitch rotation required by every game, especially for diagonal targets. Games may also use different definitions of horizontal/vertical/diagonal FOV, zoom/cropping or non-perspective projections. Do not assume a pixel always represents a constant angle across the image.

### Diagonal targets and camera rotation conventions

For a centered perspective camera, reconstruct the ray proportional to `(x, y, 1)`:

```text
x = (2 * dx / width)  * tan(HFOV / 2)
y = (2 * dy / height) * tan(VFOV / 2)
```

Under a common yaw-first convention, with X right, Y up and Z forward, one decomposition is:

```text
yaw   = atan(x)
pitch = atan2(y, sqrt(1 + x*x))
```

Angles and trigonometric inputs are in radians. The pitch expression accounts for horizontal deflection; simply using `atan(y)` describes a different independent component. The correct decomposition, rotation order and signs depend on the game's camera coordinate system. This convention is an example, not a universal game-camera rule; some integrations need a different rotation or projection model.

For a non-centered aiming reference, reconstruct both the target ray and the calibrated aiming-reference ray using the camera's actual optical center/intrinsics, then solve the required rotation under that game's conventions. Subtracting pixel coordinates alone does not make an off-center aiming reference the optical center, nor does it make `atan(target_pixel_offset)` an exact angular difference. The ROI/full-frame mapping remains necessary in either case.

Submit the resulting relative error using truthful capture/receipt times on the same session clock. ABCurves resolves it against its virtual origin at receipt. It cannot compensate for physical motion during detector latency. See [observations](../advanced/observations.md) for reference-window and ordering rules.

## Output: angular displacement to device movement

At fixed game/device sensitivity and camera settings, send a known signed device movement, measure the resulting camera rotation, and derive device counts per degree independently for X and Y. For example, 100 counts producing 5 degrees means 20 counts/degree. Repeat in both directions and at representative speeds; acceleration or other nonlinear settings may require a richer calibration than a constant gain.

For each displacement component:

```text
angle_radians = abcurves_displacement * 0.0003509487083280618
angle_degrees = angle_radians * 180 / PI
device_counts = angle_degrees * device_counts_per_degree
```

100 common counts correspond to approximately 2.01079 degrees, or 40.2158 device counts at 20 counts/degree. Apply the device's axis signs explicitly; a downward-positive Y output may require negation.

Carry the fractional remainder between 1 ms samples when emitting integers. Rounding each small sample independently can erase movement or accumulate bias. Choose rounding, limits and transport appropriate to the device; the planner output itself remains smooth angular geometry. Your application schedules timestamps and transmits reports.

FOV and capture resolution are needed for image-to-angle conversion. Measured device counts/degree can be reused until sensitivity, acceleration, zoom scaling, device configuration or other settings affecting the camera response change. A pure viewport-size change affects the image mapping, not inherently the calibrated angular device gain.

## Optional Renderer

The [original Renderer](../advanced/renderer.md) provides learned texture and integer quantization with persistent residual state. It needs its own 256-report profile and a calibrated positive radians-per-native-count gain. For a constant gain, `radians_per_native_count = (PI / 180) / device_counts_per_degree`. The public Renderer takes one gain, so differing per-axis gains require a deliberate application calibration strategy; it does not accept two independent gain fields. Planner Only output can be converted per axis directly.

Renderer reports are already native integer counts for the supplied calibration. Do not apply the above output conversion a second time. The Renderer does not automatically invert Y, discover your hardware, schedule output, or transmit reports. Preserve consistent coordinate signs through profile, input and physical output.
