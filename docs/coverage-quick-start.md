# Coverage quick start

gcov/lcov coverage for the host test build, driven from `scripts/`. Everything here runs from
a terminal — there is no IDE setup to do, and no editor extension is required.

> This document used to be written around the VS Code Test Explorer and the Coverage Gutters
> extension. That workflow is not used on this project and the instructions had gone stale, so
> they were removed rather than left to rot. `docs/vscode-test-integration.md` was deleted for
> the same reason.

## Generate a report

```powershell
cmake --build --preset host
ctest --preset host
.\scripts\Generate-Coverage-Report.ps1
```

`Generate-Coverage-Report.ps1` prints a per-component summary:

```
PicoConfigStorage    [##############------------------------------------]  27.8%
PicoDCCLocos         [############################----------------------]  57.0%
PicoDCCController    [###################################---------------]  70.3%
...
Overall Coverage: 64.96%
```

The percentages above are illustrative, not current. Run the script for real numbers —
`docs/test-coverage-report.md` is a captured snapshot from one particular run and is not
regenerated automatically.

## The other scripts

| Script | What it does |
|---|---|
| `Generate-Coverage-Report.ps1` | The per-component text summary above |
| `Generate-LCov-Coverage.ps1` | Produces `lcov.info`, for any tool that consumes lcov |
| `Convert-Coverage-For-VSCode.ps1` | Emits `.gcov` files beside their sources, plus `lcov.info` |

`docs/coverage-scripts-overview.md` covers what each one does in detail.

`Convert-Coverage-For-VSCode.ps1` is named for the editor extension it was written to feed. The
`.gcov` files it produces are plain text and readable on their own, so the script is still
useful even without that extension — read them directly, or point any lcov-aware tool at
`lcov.info`.

## Reading a `.gcov` file

Each line is prefixed with its execution count, `#####` for an executable line that never ran,
or `-` for a line that is not executable:

```
        -:   14:void PicoDccTrack::setPower(bool on)
       42:   15:{
       42:   16:    power_on = on;
    #####:   17:    // never reached by any test
```

So `grep -n '#####' <file>.gcov` lists exactly the uncovered lines.

## Coverage after a code change

Coverage data is stale the moment the code changes. Rebuild, re-run, regenerate — in that
order. A stale `.gcda` will produce a confident and wrong report rather than an error.

## What to aim for

There is no enforced gate. As a rough guide, the components that put current on the rails —
`PicoDCCTrack`, `PicoDCCController`, `PicoDCCEX`, `PicoDCCLoco` — deserve materially better
coverage than the display and config layers, because a defect in them moves a locomotive.

Coverage is a floor, not a target. `test/pico_dcc_wire_format_tests.cpp` and
`test/pico_dcc_pio_tests.cpp` exist precisely because a line can be fully covered and still put
the wrong bytes on the rails — see issues #31 and #33.
