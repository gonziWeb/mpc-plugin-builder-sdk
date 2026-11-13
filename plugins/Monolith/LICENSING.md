# Monolith: licences

| Part | Where | Licence |
|---|---|---|
| Monolith (plugin code, `rig_dsp.h`, tools, synthetic IRs) | `src/Monolith.cpp`, `src/rig_dsp.h`, `src/nam_bridge.h`, `src/nam_engine.cpp`, `tools/`, `Content/IR/` | MIT, (c) Gonzi Audio |
| Neural Amp Modeler Core | `src/NAM/` (unchanged, commit d3189ed, 2026-11-10) | MIT, (c) 2023 Steven Atkinson |
| Eigen | `src/Eigen/`, `src/unsupported/Eigen/FFT` (unchanged, commit bc3b398) | MPL-2.0 (some files BSD / Apache-2.0, see the headers) |
| JSON for Modern C++ | `src/json.hpp` | MIT, (c) Niels Lohmann |
| Example capture | `Content/NAM/Example A2.nam` (`example_models/A2.nam` of NAM Core) | MIT, with NAM Core |
| MPC Plugin SDK framework | compiled in | MIT, inMusic |

Eigen is used as unmodified headers; MPL-2.0 asks that its source stay available, which it is here
and at https://gitlab.com/libeigen/eigen. Everything is compatible with distributing the plugin
under MIT. The licence texts travel with the plugin in `Content/Licences/`.

Captures and impulse responses that users add are theirs: each TONE3000 capture and each IR pack
has its own licence, and none of them is distributed with Monolith.
