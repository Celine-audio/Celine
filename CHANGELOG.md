# Changelog

All notable changes to Céline are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Fixed

- The window now reopens at the size it was left at. `setResizeLimits` constrains the bounds it finds.
The stored size was read after that call, so it returned the minimum that had just been written, and every instance opened at its smallest.
- The About window's format marks are placed after their artwork is loaded rather than before.

## [1.0.0] — 2026-09-01

First release.

[Unreleased]: https://github.com/Celine-audio/Celine/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Celine-audio/Celine/releases/tag/v1.0.0
