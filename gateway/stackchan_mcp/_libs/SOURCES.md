# Bundled Native Libraries

This directory contains pre-built native shared libraries that the
gateway needs on platforms where the system package manager does not
typically ship them. They are loaded at import time by
`stackchan_mcp/__init__.py` via `os.add_dll_directory()` (Windows) so
that `ctypes.util.find_library()` calls inside Python wrapper packages
(e.g. `opuslib`) resolve to the bundled copy without any user setup.

## Why bundle?

The Python wrapper packages that depend on these libraries (currently
`opuslib`, pulled in via the `[tts]` and `[stt]` extras) only ship
Python bindings — they do **not** ship the underlying native library.
On Linux and macOS most users already have `libopus` available through
their distro's package manager (`apt install libopus0`,
`brew install opus`, etc.), but on Windows there is no equivalent
default install path, which means a plain `pip install stackchan-mcp[tts]`
fails at runtime with `Could not find Opus library. Make sure it is
installed.` even though the Python wrappers installed cleanly.

Bundling the Windows binary in the wheel removes that footgun: every
Windows user who installs `stackchan-mcp[tts]` (or `[stt]`) gets a
working installation on the first try, with no extra `vcpkg` /
`conda install -c conda-forge libopus` / manual DLL placement step.

The decision to bundle (vs. download at install time vs. require source
build) was made on these criteria:

| Criterion | Verdict for libopus |
|---|---|
| Maturity of the dependency | Mature (Opus is a frozen IETF codec, RFC 6716, 2012) |
| Frequency of security advisories | Very low (the codec parser is small and well-audited) |
| File size | ~480 KB — fits comfortably in the wheel |
| Re-distribution license | BSD 3-clause (Xiph) — redistribution allowed with attribution |
| Long-term availability of upstream | Excellent (Xiph.Org maintains the source indefinitely) |

If any of those change (e.g. a future ML-based bundle that ships
hundreds of MB), revisit and consider the "CI downloads a pinned
version at build time" approach instead.

## opus.dll

| Field | Value |
|---|---|
| Architecture | x86_64 (`win_amd64`) |
| License | BSD 3-clause + Xiph extension — see <https://opus-codec.org/license/> |
| Provenance | Built from upstream Opus source by the publish workflow via vcpkg |
| Build command | `vcpkg install opus:x64-windows` (CI runner: `windows-latest`) |

### Provenance note

`opus.dll` is **not** tracked in git. The publish workflow
(`.github/workflows/publish.yml`, job `build-windows-wheel`)
bootstraps a fresh vcpkg checkout on a `windows-latest` runner,
runs `vcpkg install opus:x64-windows`, copies the produced
`opus.dll` into `stackchan_mcp/_libs/`, and logs its SHA256 to the
job log so reviewers can spot vcpkg-side binary drift before a tag
publishes. The wheel build that follows picks the DLL up via
`tool.hatch.build.targets.wheel.artifacts` in
`gateway/pyproject.toml`, and the resulting wheel is renamed from
`*-py3-none-any.whl` to `*-py3-none-win_amd64.whl` so pip resolves
it only on Windows x64 installs.

Builds on the Ubuntu runner (sdist + the `py3-none-any` wheel they
produce) do not place a DLL under `_libs/`, so those distributions
ship clean — non-Windows installs and non-x64 Windows installs
either fall back to a system `libopus` (Linux/macOS) or get a
clean "no compatible wheel" install-time message (Windows ARM64 /
x86 32-bit).

### Local development

If you need a local Windows checkout to test the bundling path
(running `uv build` outside CI), mirror the CI step by:

1. Installing libopus via vcpkg (`vcpkg install opus:x64-windows`)
   and copying the produced DLL into `stackchan_mcp/_libs/opus.dll`.
2. Or downloading the same `opus.dll` from a release artifact
   uploaded by the publish workflow.
3. Or installing system libopus and copying it into the directory.

The DLL is gitignored (see `gateway/.gitignore`) so a local copy
never sneaks into a commit.

## License compliance

The Opus codec is distributed under the 3-clause BSD license (with the
optional Xiph patent grant), which permits redistribution in source or
binary form provided the copyright notice and license text are
preserved. The canonical notice ships at the top of every gateway
distribution as `LICENSE-THIRD-PARTY` (declared in
`gateway/pyproject.toml`'s `license-files`); the same text is
reproduced below as the bundling-rationale narrative for readers of
this document.

```
Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic,
                    Jean-Marc Valin, Timothy B. Terriberry,
                    CSIRO, Gregory Maxwell, Mark Borgerding,
                    Erik de Castro Lopo

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

- Neither the name of Internet Society, IETF or IETF Trust, nor the
  names of specific contributors, may be used to endorse or promote
  products derived from this software without specific prior written
  permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
