[English](documentation-audit.md) | [简体中文](zh-CN/documentation-audit.md)

# Bilingual documentation audit

Audit date: 2026-11-14. This V2.1 revision changes documentation, documentation tooling and its CI job. Protocol, class, network adapter, tests of driver behavior and public C interfaces are unchanged. Work remains local on main; the CI job is configured but has not been executed remotely for this revision.

## Maintained document inventory

| English primary | Complete Simplified Chinese translation |
| --- | --- |
| [README](../README.md) | [README](../README.zh-CN.md) |
| [Examples](../examples/README.md) | [Examples](../examples/README.zh-CN.md) |
| [Architecture](architecture.md) | [Architecture](zh-CN/architecture.md) |
| [Compatibility](compatibility.md) | [Compatibility](zh-CN/compatibility.md) |
| [Latency probe](latency-probe.md) | [Latency probe](zh-CN/latency-probe.md) |
| [Porting](porting.md) | [Porting](zh-CN/porting.md) |
| [Protocol audit](protocol-audit.md) | [Protocol audit](zh-CN/protocol-audit.md) |
| [Sources](sources.md) | [Sources](zh-CN/sources.md) |
| [Validation](validation.md) | [Validation](zh-CN/validation.md) |
| [This audit](documentation-audit.md) | [This audit](zh-CN/documentation-audit.md) |

The seven pre-existing technical documents retain their technical scope and historical sections. English prose was reflowed to one physical line per paragraph/list item; commands, logs and required code breaks were preserved. README was expanded to cover architecture, protocols, integration, lifecycle, diagnostics, evidence levels, limitations and licensing. All nine pre-existing documents gained full translations; this audit adds one new pair. English is primary, with Chinese navigation in each translation.

## Exceptions and provenance

No maintained Markdown document is exempt. Original upstream legal notices in the plain-text LICENSES directory and the top-level LICENSE remain unchanged and untranslated. They are not project-maintained technical Markdown. Ignored build output and external dependency checkouts are generated/reference material, not formal documentation. Historical image paths, hashes, source revisions and external UART observations remain as historical evidence; they are not claims that the current revision was flashed.

## Consistency review

Both languages retain each document's major sections, public API names, commands, configuration, test status, limitations, warnings and license references. The review preserves Apple NCM RX-only/raw Ethernet TX, Legacy RX prefixes, first-Bulk-IN readiness, Carrier formats, external USB configuration selection, Interface Claim ownership, failed-cancel quarantine and persistent netif lifetime. Driver readiness remains separate from USBMUX/Lockdown trust and CarPlay session readiness.

The checker compares heading-level sequences, fenced code/commands, inline technical literals and external Markdown reference URLs. It checks every maintained Markdown pair, navigation, naming, relative links, heading fragments, primary-language separation and obvious hard wraps. These mechanical indicators complement human translation review; identical literals and heading counts do not prove semantic equivalence. External websites are not contacted and cannot cause a network-dependent CI failure. Bare historical filesystem paths and source identifiers are reviewed in context rather than required to exist on the reader's machine.

## Local verification

The Windows Python documentation audit and the WSL Ubuntu 24.04 Python audit pass for all ten pairs, with zero Markdown exemptions. The checker's isolated temporary-Git mutation suite passes on both systems; it covers missing/orphan translations, navigation, file/heading links, hard wraps, artificial breaks, naming, code/literal/reference drift and language separation. CTest Debug and ASan/UBSan each pass all three driver tests using the actual external CherryUSB checkout at the pinned revision. These are local results, independent of historical public CI.

Reproduce the documentation checks:

```sh
python3 scripts/test-check-docs.py
python3 scripts/check-docs.py
```

Reproduce the driver checks from the Luban-Lite submodule layout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCHERRYUSB_ROOT=../cherryusb
cmake --build build -j8
ctest --test-dir build --output-on-failure
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DIPHETH_SANITIZE=ON -DCHERRYUSB_ROOT=../cherryusb
cmake --build build-san -j8
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-san --output-on-failure
```

The host workflow runs the documentation mutation suite and audit in a separate job, retaining the existing plain/sanitized driver matrix. Formatting configuration disables prose wrapping and markdownlint's line-length limit. Current-revision hardware regression is NOT_RUN; sustained load, physical reconnect, legacy-mode and broader-device evidence remain pending as described in the validation record.
