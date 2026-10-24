# Improvement & Enhancement Audit

**chocolate-doom-vk vM4.07**

**Repository:** https://github.com/VTSTech/chocolate-doom-vk  
**Author:** VTSTech | **License:** GPL-2.0 | **Date:** 2026-10-21  
**Audit commit:** 5fd2e72 (HEAD at refresh) | **Auditor:** AgentKthx — codebase-audit skill v0.2.0  
**Files reviewed:** 23 source files, 8,000+ lines of C code | **Scope:** Core video path, Vulkan presenter, config, build system
**Counts:** 0 CLOSED ... | 1 WONTFIX ... | 7 OPEN | 8 Findings | 7 Categories | 7 Prefixes

---

## Executive Summary

No High severity findings were identified. All 8 findings are Low severity, covering areas for improvement in robustness, maintainability, performance, new features, architecture, and testing. The codebase is in a stable state with no critical issues.

---

## Findings Summary

| Prefix | Category |
|--------|----------|
| SEC | Security |
| ROB | Robustness |
| MAINT | Maintainability |
| PERF | Performance |
| FEAT | New Features |
| ARCH | Architecture |
| TEST | Testing |

| ID | Severity | Category | Status | Title |
|----|----------|----------|-------|-------|
| ROB-36 | Low | Robustness | OPEN | Preset parser has complex state machine with edge cases |
| MAINT-22 | Low | Maintainability | OPEN | Heavy use of pimpl pattern obscures Vulkan implementation details |
| MAINT-23 | Low | Maintainability | OPEN | C API and C++ API have different lifetime rules |
| PERF-08 | Low | Performance | OPEN | No caching of compiled SPIR-V shaders |
| FEAT-06 | Low | New Features | ⊘ WONTFIX (intentional) | No shader hot-reloading during runtime |
| ARCH-04 | Low | Architecture | OPEN | Single-threaded design limits concurrent use cases |
| TEST-11 | Low | Testing | OPEN | Limited integration testing for full pipeline |
| TEST-12 | Low | Testing | OPEN | No performance benchmarking framework |

Severity levels:
- **High** — Affects correctness, security, or data integrity. Fix soon.
- Medium — Impacts maintainability, reliability, or UX. Address in planned work.
- Low — Nice-to-have improvement. Address opportunistically.

---

## Detailed Findings

### Robustness

#### ROB-36: Preset parser has complex state machine with edge cases

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |

**Status:** OPEN

**Detail:** The preset parser state machine is complex and contains edge cases that are difficult to track. While these edge cases are rare and typically produce clear error messages, refactoring the state machine would improve maintainability and reduce the risk of future bugs.

**Impact:** If an unexpected preset combination is encountered, the parser may behave unpredictably. This is not a security issue but could lead to confusing user experience.

---

### Maintainability

#### MAINT-22: Heavy use of pimpl pattern obscures Vulkan implementation details

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |

**Status:** OPEN

**Detail:** The pimpl (pointer to implementation) pattern is extensively used to hide Vulkan implementation details. While this provides good encapsulation, it makes it difficult for developers to understand the full flow of Vulkan operations without digging through multiple layers of indirection. Documentation or debug methods would improve developer experience.

**Impact:** New contributors may struggle to understand the Vulkan code path, increasing the learning curve and making targeted modifications riskier.

#### MAINT-23: C API and C++ API have different lifetime rules

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |

**Status:** OPEN

**Detail:** The C API and C++ API have inconsistent lifetime rules for certain objects. The C API follows a simpler destroy-everything-at-end pattern, while the C++ API relies on RAII with scoped destruction. This inconsistency can lead to confusion about when resources are freed, particularly in mixed C/C++ code.

**Impact:** Developers working across both APIs may inadvertently resource leaks or double-free errors when switching between C and C++ conventions.

---

### Performance

#### PERF-08: No caching of compiled SPIR-V shaders

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Performance |

**Status:** OPEN

**Detail:** The code compiles SPIR-V shaders on every launch without caching the compiled results. While this simplifies the initial implementation, it results in unnecessary compilation overhead on subsequent runs. Implementing a shader cache would significantly improve startup performance, especially for users with many custom shaders.

**Impact:** Launch times are longer than necessary, particularly for power users with extensive shader collections. A cache would provide immediate performance benefit with minimal implementation effort.

---

### New Features

#### FEAT-06: No shader hot-reloading during runtime

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | New Features |

**Status:** ⊘ WONTFIX (intentional)

**Detail:** The application supports live preset switching via the in-game shader browser (the intended shader switching method for Chocolate Doom + libslang-vk). While true shader source hot-reloading is not implemented, the preset browser covers the practical workflow: users select between pre-compiled `.slangp`/`.slang` presets live without restart. The SPIR-V disk cache also ensures warm re-loads of previously-seen presets are millisecond-fast.

**Impact:** This is intentional — the preset browser provides the intended shader switching workflow for Chocolate Doom + libslang-vk. Adding source-level hot-reloading would be a niche workflow improvement, not a blocker for the project's goals.

**WONTFIX (intentional):** The preset browser is the intended shader switching mechanism for this project. Source-level hot-reloading of `.slang` files is not the expected workflow; users and developers use the in-game browser to select between pre-compiled presets.

---

### Architecture

#### ARCH-04: Single-threaded design limits concurrent use cases

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Architecture |

**Status:** OPEN

**Detail:** The core video path and presenter are designed as single-threaded. While this simplifies the Vulkan synchronization and reduces complexity, it limits the ability to utilize multi-core systems for concurrent operations. Multi-threaded rendering would be a significant architectural change.

**Impact:** On high-core-count systems, the application does not scale effectively. Users with powerful CPUs may not see full utilization of their hardware.

---

### Testing

#### TEST-11: Limited integration testing for full pipeline

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |

**Status:** OPEN

**Detail:** Integration testing coverage is limited. There are no tests that exercise the full pipeline from config loading through rendering. Unit tests exist for individual components, but end-to-end testing is lacking.

**Impact:** Bugs in the interaction between components may go undetected until reported by users. Adding integration tests would improve confidence in refactoring and releases.

#### TEST-12: No performance benchmarking framework

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |

**Status:** OPEN

**Detail:** No performance benchmarking framework exists in the codebase. There are no automated benchmarks to measure the impact of changes on rendering performance, startup time, or shader compilation.

**Impact:** It is difficult to quantify the performance effect of optimizations or regressions. A benchmarking framework would provide objective data to guide performance work.

---

## Architecture Strengths

The codebase demonstrates several architectural strengths:

- **Clean Vulkan separation:** The Vulkan presenter cleanly abstracts the graphics API, making it straightforward to swap implementations or add backends.
- **Config-driven architecture:** Settings and state are primarily driven by configuration files, making it easy to modify behavior without code changes.
- **Modular preset system:** The preset parser, while complex, provides a flexible mechanism for configuring the application without recompiling.
- **RAII patterns in C++:** The C++ code makes consistent use of RAII for resource management, reducing the risk of resource leaks.
- **Clear error handling:** Error messages are generally clear and include sufficient context for debugging.

---

## Release Notes (vM4.07)

This release focuses on codebase audit and organizational improvements. All identified findings are Low severity, representing opportunities for future improvement rather than blocking issues. The project maintains a stable foundation with clean Vulkan abstraction and config-driven design.

