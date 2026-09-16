# Contributing

Bug fixes, review findings, documentation, reproducible-build results and hardware
test reports are welcome. Thunder Den aims to be easy to audit with AI before use.
Every change must justify its review and maintenance cost; a useful feature can
still be too costly to include.

## What we accept

- **Simplicity first.** Choose the smallest clear solution to an existing problem.
  Avoid speculative abstractions, configuration knobs and fallback paths.
- **Keep the scope narrow.** Favor correctness and justified simplification over
  new features. Reduce unnecessary code, inputs, privileges and dependencies.
- **Justify dependencies.** Explain why existing components cannot meet the need,
  account for transitive dependencies and pin build inputs. Reuse the project's
  established cryptographic primitives rather than implementing replacements.
- **Security needs a reason.** Describe a credible attack or failure path, the
  existing protections and how the change helps. Its benefit must justify its
  added complexity and attack surface.
- **Refactors need evidence.** Explain a concrete improvement in correctness or
  ease of review. Keep them separate from behavior changes; avoid cosmetic rewrites.

## How to submit

1. Open an issue before starting a new feature, dependency, architecture change
   or substantial refactor so its scope and cost can be discussed.
2. Keep each pull request focused. Explain the problem, why the solution is needed
   and its effect on dependencies, runtime permissions and build reproducibility.
3. For behavior changes, include a focused regression check. Run the relevant
   [build and tests](docs/BUILD.md) and report the commands and results; for
   documentation-only changes, check wording and links.
4. Make small, coherent commits with one purpose each. Follow the existing message
   style, such as `fix: reject inconsistent request fields` or
   `docs: clarify build steps`. Keep unrelated formatting, generated build products
   and real secrets out of commits.

Documentation and comments should explain the current design to a new reader,
including the reason for non-obvious choices. Preserve a small, inspectable system
as the project grows.
