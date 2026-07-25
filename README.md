# pimio

[![CI](https://github.com/dtmland/pimio/actions/workflows/ci.yml/badge.svg)](https://github.com/dtmland/pimio/actions/workflows/ci.yml)

A local-first photo and video organizer for repairing and maintaining
chronological media libraries. Built with C++20, Qt 6, and QML.

## Building

Requires CMake >= 3.24, Ninja, a C++20 compiler, and Qt 6 (>= 6.4 to build;
6.8.3 is the version verified by CI).

```
cmake --preset default
cmake --build --preset default
ctest --preset default
```

See [docs/conventions.md](docs/conventions.md) for repository layout, naming,
and the additional Linux X11 test run.

## Documents

Planning:

- [Product vision](pimio.md)
- [1.0.0 release plan](pimio-v1.md)
- [1.0.0 implementation plan](pimio-v1-implementation.md)
- [v1 tools, environment, and CI strategy](pimio-v1-tools-environment.md)
- [2.0.0 release plan](pimio-v2.md)

Policy and reference:

- [Repository conventions](docs/conventions.md)
- [Supported platform policy](docs/supported-platforms.md)
- [Dependency bill of materials](docs/dependency-bom.md)
