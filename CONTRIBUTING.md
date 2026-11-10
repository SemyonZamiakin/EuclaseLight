# Contributing to Euclase Light

Thanks for your interest. Bug reports, hardware test reports and focused pull requests are welcome.

## Before you start

- For anything larger than a small fix, open an issue first so the approach can be agreed.
- Keep changes focused: one topic per pull request.

## Build and test

```
./build-core.ps1   # builds the C++ core and runs its tests
./package.ps1      # additionally builds the WPF app and the release archive
```

Requirements: Windows x64, MSYS2 mingw64 `g++`, .NET SDK 10. The core is covered by `core/tests/test_core.cpp`.

A pull request is expected to keep the build warning-free (`-Wall -Wextra`) and the test suite green. New behaviour needs a test.

Changes to the bus layer (`core/src/guard.cpp`) affect hardware safety and need a test that shows the protected ranges stay protected.

## Style

- Match the surrounding code: naming, formatting and comment density.
- No new third-party dependencies without discussion.
- Do not commit binaries, build output or secrets.

## Commits and pull requests

- Write the commit subject in the imperative mood ("Fix X", "Add Y"), under about 72 characters, with a short body that explains why.
- Describe in the pull request what you tested and on what hardware or OS.

## Reporting results from your hardware

For hardware-dependent behaviour, the most useful report contains: the exact device model, OS version, what you expected, what happened, and logs where available.

## License

By contributing you agree that your contribution is licensed under the MIT license of this repository.
