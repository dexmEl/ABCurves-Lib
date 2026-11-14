# ABCurves integration assistant

Use this guide when a user asks an AI agent to implement ABCurves in their project, asks "How do I implement this?", or needs help setting up calibration. Your job is to gather the missing information, follow the appropriate branches below, and implement and verify the integration in the user's application.

For a focused API question, answer it directly. For changes to ABCurves itself, follow [CONTRIBUTING.md](CONTRIBUTING.md). This guide concerns application integration; training and changing the library are separate tasks.

## Connect the hosted documentation MCP

The public, read-only documentation MCP is available at **https://mcp.nanoed.net/abcurves**. No login or API key is required. Add it with the command for your client:

```shell
# Codex CLI
codex mcp add abcurves --url https://mcp.nanoed.net/abcurves

# Claude Code (available across your projects)
claude mcp add --transport http --scope user abcurves https://mcp.nanoed.net/abcurves
```

Then ask the agent: "Use the ABCurves MCP to read AGENTS.md in full, then help me integrate ABCurves into this project." Follow `read_doc` pagination until `next_start_line` is null. The server supplies documentation and examples; inspect, implement and run tests in the user's local project. Check the snapshot version against the installed library. See the [MCP guide](mcp/README.md) for local hosting and deployment details.

## How to conduct the conversation

- Inspect the host project's instructions, language, build files, existing target pipeline, calibration and output code before asking questions. Resolve links in this file relative to the ABCurves checkout or package, not the host project.
- Reuse answers from the conversation and values verified in the project. Ask only about information that is missing, ambiguous or inconsistent.
- Ask one or two focused questions at a time. Explain unfamiliar choices briefly, then use the answer to select the next step. Do not present the whole questionnaire at once.
- Track the selected model, language/platform, installation route, input units/mapping, output conversion, clock and calibration readiness. Distinguish measured values from examples and unknowns.
- Offer documented defaults for optional features. Do not invent FOV, sensitivity, device gain, profile data or measured latency.
- Once enough information is available, proceed with the requested implementation. Do not repeatedly ask whether to start. If measurements are missing, implement the independent pieces and a logging/calibration path, then ask for the specific missing measurement. Describe the result as incomplete until that measurement is integrated and verified.

## 1. Start with model selection and calibration status

If neither answer is known, use an opening like:

> Which model would you like to use: **8 ms, 16 ms or 32 ms**? These are movement commitment durations; all three produce **1 ms samples**. If you're unsure, I recommend starting with **32 ms**.
>
> Do you already have calibration for both your target coordinates and your movement output, or do you need help setting it up? "Partial" or "not sure" is fine.

If either answer is already known, ask only the missing question.

| Answer | Next action |
|---|---|
| 32 ms | Record the selection and use it consistently. It is the recommended starting point, longest commitment interval and default comparison baseline; do not promise that it is universally more natural or accurate. |
| 16 ms | Record the selection and use it consistently. It is the middle interval and provides more frequent planning opportunities than 32 ms. |
| 8 ms | Record the selection and use it consistently. Describe it as the shortest commitment interval, which provides more frequent opportunities for new observations to affect a later plan. Do not imply that it can rewrite generated movement or guarantees faster execution. |
| Unsure | Explain the commitment trade-off and recommend 32 ms as the documented starting point. If the user delegates the choice, select 32 ms and state it. |
| Wants to match camera FPS or device polling rate | Explain that model duration is independent of both. Every model returns 1 ms samples; ask about the desired commitment duration instead. |
| Calibration exists | Inspect its units, axis signs, settings and conversion code in steps 3 and 4. A sensitivity number alone does not establish calibration. |
| Calibration missing, partial or uncertain | Determine which input/output conversions are required, then guide only the missing calibration steps. |

Recommend the 32 ms baseline because the maintainer reports stronger human-likeness/AUC results than for the 16 ms and 8 ms variants. Prioritize that comparison when choosing a starting model; earlier planning decisions or lower synthetic target error alone do not establish more human-like movement.

All three models return 1 ms samples. A shorter commitment interval gives the planner an earlier next decision boundary; it does not change movement already generated and does not set the camera or device update rate. Use the model-selection notes in [Quick Start](docs/quick-start.md#1-choose-a-model) when describing trade-offs. A user-reported comparison found more overshoot and direction changes for earlier 16 ms and 8 ms bundles than for 32 ms, but that result is specific to those bundles and that test. Do not carry it over to retrained bundles or claim a universal naturalness, smoothness, accuracy or responsiveness ranking. The published evidence does not include a matched human-based AUC evaluation for the retrained bundles; their synthetic target-error results do not establish human-likeness.

## 2. Identify the host project and installation route

If inspection did not establish them, ask:

> What language and OS/architecture does your project use, and do you already have the ABCurves source, SDK, DLL or Python wheel?

Also locate the target-observation callback and movement-output interface. If their location is unclear, ask where targets enter the application and how movement is currently sent.

Use the guide matching the answer:

| Project | Implementation route |
|---|---|
| C++ with a Windows DLL | [C++ guide](docs/getting-started/cpp.md): header-only `abcurves_dll` wrapper. |
| C++ linking the native engine | [C++ guide](docs/getting-started/cpp.md): C++20 and installed `abcurves::native` CMake target. |
| C | [C guide](docs/getting-started/c.md): C ABI, dynamic DLL loading or installed `abcurves::c` target. |
| Python | [Python guide](docs/getting-started/python.md): `abcurves_native`, Python >=3.10 and a matching native runtime. The separate `Native/abcurves` package is the reference implementation. |
| Rust | [Rust guide](docs/getting-started/rust.md): local `abcurves-native` safe crate and its sibling `abcurves-sys`. |
| Another language | Inspect the [C header](API/c/include/abcurves.h) and existing host FFI before proposing an adapter. Do not invent an official binding. |

Windows x64 is qualified; Linux/macOS portability is unverified. Do not assume a Windows DLL works on another platform. Use the [source build guide](docs/getting-started/building.md) or [packaging guide](packaging/README.md) for the selected route. No PyPI or crates.io release is claimed; do not invent registry installation commands.

The ABI 1.5 Windows DLL embeds the 8/16/32 ms planner models and Renderer model. Native C++ source integrations and external-model workflows can load a bundle directory containing `manifest.json` and `model.abcmodel`. Verify what the user's package actually contains before choosing an embedded API or external path. Installation does not require model training/export.

## 3. Establish input units and coordinate calibration

Ask if the answer is not visible in the target pipeline:

> What does your detector or target source currently return: pixels, radians/degrees, ABCurves common angular counts, or device counts?

ABCurves expects finite relative angular error in **common angular counts**, with **X right and Y up**. One common count is exactly `0.0003509487083280618` radians. Its virtual origin is unrelated to physical cursor position.

| Existing input | Next action |
|---|---|
| Common angular counts | Verify the constant, axis signs and relative-error semantics; skip pixel calibration. |
| Radians or degrees | Convert radians to common counts by dividing by the constant; convert degrees to radians first. Verify axes/reference. |
| Pixels | Follow the pixel branch below. |
| Device/native counts | Use verified device-to-angle calibration from step 4, then convert angle to common counts. Do not pass device counts directly. |
| Unknown | Inspect the producing function or a sample record and identify its units before connecting it to the planner. |

### If the input is pixels

Collect only missing details, in small follow-up questions:

1. Actual full viewport width/height and camera FOV, including whether that FOV is horizontal, vertical or diagonal and whether zoom changes it.
2. Detector crop/ROI offset, resizing and padding/letterboxing, plus the aiming-reference point and image axis convention.

Then implement the [calibration guide](docs/guides/calibration.md#input-image-error-to-angular-target-error): undo detector transforms, map to the full viewport, solve the angular error using the camera's projection, and convert to common counts. For the usual centered image reference, `dx = full_x - width/2` and `dy = height/2 - full_y` give right/up error.

Use the full viewport dimensions with its FOV, including when detection uses a small ROI. The guide's independent-axis perspective formulas are a starting point; verify the application's rotation convention for diagonal targets. An off-center aiming reference requires target/reference rays and the actual optical center, not just substituting an off-center pixel origin.

If FOV or projection is unknown, help locate camera settings/intrinsics or an existing verified conversion. Do not substitute an arbitrary FOV, detector crop size, DPI or sensitivity. Build the mapping interface and log-only example while requesting the missing camera information. Skip this branch entirely when the source already supplies calibrated angles.

## 4. Establish output calibration

Ask if inspection has not answered it:

> Does your output API consume angular movement or integer device counts, and do you already have a measured counts-per-degree conversion for X and Y?

| Output situation | Next action |
|---|---|
| Application consumes angular displacement | Convert common counts to the application's angular units. Device calibration and Renderer are unnecessary for this path. |
| Existing calibrated device adapter | Verify units, per-axis gains, signs, fractional remainder handling, device limits and applicable camera/sensitivity settings. Reuse it if correct. |
| Device output with missing calibration | Walk through the measurement procedure below. |
| User requests the original Renderer | Follow the Renderer branch after establishing device gain. |

### If device calibration is missing

Explain that ABCurves does not discover sensitivity or perform calibration movements itself. Help the user build or use a calibration utility through their existing device interface:

1. Record the sensitivity, zoom/camera mode, acceleration and device settings being measured. Keep those settings consistent during each measurement.
2. Send a known signed movement on one axis and measure the resulting signed camera rotation in degrees, using camera orientation or a verified image-to-angle mapping. Raw pixel distance alone is insufficient.
3. Calculate the positive gain magnitude `device_counts_per_degree = abs(sent_counts / measured_degrees)` for nonzero measurements. Record the axis direction separately.
4. Repeat for X and Y, in both directions and at representative movement sizes/speeds. If the response changes with speed or direction, investigate acceleration/nonlinearity instead of treating one constant as calibrated.
5. Store the measured gains, axis mapping and applicable settings in application configuration. Verify that known movements reproduce the expected angles.

Ask for the measured counts and resulting rotation if the user must perform the experiment. Do not claim to have measured hardware from a numerical smoke test. Example only: **100 device counts producing 5 degrees means 20 counts/degree**; this is not a default gain.

For manual output conversion, apply per axis:

```text
angle_radians = sample.displacement_axis * 0.0003509487083280618
device_counts_float = angle_radians * (180 / PI) * device_counts_per_degree_axis
```

Apply the device's axis sign and carry fractional remainders across samples and segments when emitting integers. Use displacement, not absolute virtual position. Respect device report limits without silently discarding movement. Choose an explicit strategy if the transport cannot consume 1 ms samples; do not change the meaning of the model interval to fit it.

Revisit device calibration when sensitivity, acceleration, zoom scaling or device response changes. A viewport-size change requires updating pixel mapping but does not inherently change device counts per degree.

### If the original Renderer is requested

Default to Planner Only with application conversion unless the user requests Renderer or the existing project uses it. Read the [Renderer guide](docs/advanced/renderer.md) before implementing this branch.

- Ask whether there are **exactly 256 chronological signed int16 X/Y native-count profile reports** and a measured positive **radians-per-native-count** gain. If either is missing, help collect/derive it; synthetic example profiles only test execution.
- For constant device gain, `radians_per_native_count = (PI / 180) / device_counts_per_degree`. This is the inverse angular scale, not counts per degree.
- Renderer accepts one gain, not independent X/Y gains. If axis gains differ, resolve that calibration strategy explicitly or use Planner Only with per-axis conversion.
- Normalize the profile/input to X right/Y up. Convert returned reports to the physical device's signs explicitly.
- Set the profile, begin/reset the stream, and keep one Renderer alive across segments. Render each sample once, in order. Match reports to sample timestamps by index.
- Renderer reports are already native integer counts. Do not apply angular-to-device conversion again. Planner resets do not reset Renderer state.

## 5. Confirm timestamps and execution behavior

Inspect the existing clock and scheduler first. Ask only about unresolved details, for example:

> Can your target source provide capture and receipt times on one monotonic clock, and how does your output code schedule movement reports?

Implement the [integration contract](docs/integration.md) and [observation rules](docs/advanced/observations.md):

- Use integer session microseconds from a shared monotonic epoch starting at zero. Capture must be no later than receipt; receipt order must not decrease. Translate external clock domains deliberately. Do not use wall-clock timestamps or fabricate zero capture latency.
- Align a new planner's numerical time zero with the session/output start. The controller does not read wall time or catch up automatically after startup delays or pauses. Define an explicit restart policy when execution and numerical time diverge.
- Submit observations when they arrive, independently of segment consumption. Request the next segment just before needed, with room for generation. Pre-generating many segments delays new input and can lose required relative-origin history.
- Schedule returned samples in order using their session timestamps. Each timestamp is the endpoint of a 1 ms interval. Iterate the returned sample count; idle segments and commitment remainders can be shorter than the model duration.
- Missing observations retain the previous target. On actual target loss or paused pursuit, submit explicit unavailable input; reacquire with an available observation. Neither loss nor a new observation cancels already generated movement.
- Keep generated movement and physical execution consistent. Do not silently drop a segment and continue as though it ran. A reset starts a new numerical session and requires coordinating the output queue/clock and any Renderer state.

For image feedback, evaluate application-side capture-latency compensation using measured/estimated **executed** movement, not merely generated movement. Capture/receipt timestamps alone do not compensate camera movement. Consider an angular deadband with hysteresis if noise causes on-target corrections; tune it from observed noise rather than inventing thresholds.

Do not demand physical mouse position, execution acknowledgments, camera FPS, an initialization prefix or an external movement-history feed as planner requirements. The application may use execution estimates for its own latency compensation.

## 6. Implement when the required answers are available

Use the selected [language guide](docs/README.md) and [runnable example](examples/README.md); verify exact signatures in the installed headers/binding. Adapt to the host architecture rather than replacing unrelated code.

1. Add the dependency and runtime/model discovery for the chosen installation route. Preserve license notices when distributing it.
2. Put the selected model and real calibration/settings in the host's configuration. Clearly distinguish unknown values from usable values. Keep device output disabled in an incomplete calibration path; provide logging so independent work can be verified.
3. Load one Model and create a persistent Planner plus one SegmentController per independent planning session. Use defaults for seed/prefix/options unless the project needs otherwise.
4. Connect the input adapter: detector/target -> calibrated relative common counts -> observation with capture/receipt times. Connect explicit target-loss events too.
5. Connect generation and output: next segment -> actual samples -> manual conversion **or** Renderer -> existing scheduler/transport. Keep ownership valid until consumers finish.
6. Serialize calls on the same planner/controller and coordinate shutdown. Independent sessions may use separate planners. Do not mix direct planner mutations with an attached SegmentController. Close dependent objects before parents/unloading the DLL.
7. Handle library errors through the host's error/reporting path. Preserve diagnostic details; do not hide errors by rewriting timestamps, resetting on every frame or substituting fake movement. Consult [diagnostics](docs/advanced/diagnostics.md) for recovery.

If the user only wants an explanation, provide this sequence adapted to their answers with relevant code and commands. If they ask for implementation in an available project, make the changes and run the applicable checks. Do not stop at generic pseudocode when the required interfaces and configuration are available.

### Advanced options quick reference

Keep these integration constraints explicit when an application needs nondefault setup. The exact declarations and validation rules remain in [initialization, options and data](docs/advanced/data-and-options.md), with runnable usage in the language examples.

- An initialization prefix is optional. The default is no external prefix.
- A prepared prefix contains exactly **160 finite chronological 1 ms displacement pairs**.
- The raw-prefix path accepts exactly **164 finite displacement pairs plus an explicit finite anchor**; prepare it to the 160-pair form and use the returned filtered anchor. Do not treat this prefix length as the model commitment duration.
- The default virtual initial position is `(0,0)`, the default planner seed is `7`, and diagnostic proposals are off. Preserve a user-specified seed or initial position consistently across construction, reset and reinitialization.
- Controller defaults are latest observation ordering, `max_age_us=250000`, disabled inactivity timeout (`-1`), and observation capacity `64`. Change them only when the application's timing and queue policy requires it.
- The optional Renderer requires exactly **256 chronological signed int16 X/Y native-count profile reports** and a measured positive radians-per-native-count gain. Do not invent profile data or gain; use Planner Only with application conversion when those measurements are unavailable.
- Verify exact constructor, reset, preparation and controller signatures in the selected language guide or binding; the Python feature example demonstrates both prepared and raw prefixes.

## 7. Verify and hand off

Follow the [integration validation guide](docs/guides/integration-validation.md) and scale verification to the changed integration. Check the selected model loads, observation submission succeeds, and generation returns finite samples in timestamp order. Zero displacement can be valid holding behavior; do not require every sample to move.

Verify the applicable input mapping and output conversion, axis signs, preserved fractional movement, target loss/reacquisition and scheduling behavior. Use deterministic numerical checks and the host's relevant build/tests before reporting success. Numerical/log-only tests establish wiring; physical calibration and timing need evidence from the actual application/device.

Finish by stating the selected model, where configuration and integration code live, the exact build/run commands, checks performed, and any remaining measured values or application-side validation. Clearly distinguish a runnable integration from a scaffold waiting on calibration.

## Example conversation branches

- **"16 ms, C++ on Windows, calibration is already in the project."** Inspect the existing conversions and build/output interfaces, ask only about missing clock or unit details, then implement using the appropriate C++ route. Do not restart the calibration questionnaire.
- **"8 ms, Python, no calibration, cropped detector output."** Inspect the crop transform; collect full viewport/FOV and aiming-reference details; guide X/Y device measurements. Implement the adapters and logging while measurements are gathered, then connect verified conversion to output.
- **"Not sure, choose for me; targets and output are radians."** Select 32 ms, verify angular axes/reference and timestamps, and integrate angle conversion. Skip pixel/device calibration and Renderer profile questions.
- **"32 ms with Renderer; gain measured, no profile."** Keep that model choice, validate the gain's units, and help collect the 256-report profile. Complete independent integration work and identify the profile as the remaining Renderer requirement.
