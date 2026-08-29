# Security policy

## Supported versions

Only the latest release is supported; fixes will not be backported.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting: the **Security** tab on this
repository, then **Report a vulnerability**. That keeps the report private until
a fix exists.

Please do not open a public issue for a security problem.

If a report is valid you will be credited in the release notes unless you would rather not be.

## What is in scope

Céline runs inside a host application, with the host's privileges. The parts
worth looking at are the ones that read data someone else produced:

- **`.celsch` schematics** — XML, loaded from disk and from the host's saved
  session state. Loaded through `restoreDocument()`.
- **Cabinet impulse responses** — arbitrary audio files, decoded by JUCE's
  format readers.
- **The preset folder** — a user-nominated path remembered in a properties file.

A crash, a hang or memory corruption reachable from any of those is worth
reporting.

## What is not

- **Unsigned binaries.** Releases are not code signed, so Gatekeeper and
  SmartScreen will warn. This is a known and documented state.
- **Simulation accuracy.** What the engine does not model is listed in
  [`source/CelineEngine/LIMITATIONS.md`](source/CelineEngine/LIMITATIONS.md).
- Vulnerabilities in JUCE or another dependency: please report those upstream.