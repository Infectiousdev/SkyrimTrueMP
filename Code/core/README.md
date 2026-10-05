# Core library

Header-only containers, buffers, serialization, allocators and small utilities used by every part of
the game. Original code, GPLv3-or-later, written for SkyrimTrueMP.

It replaces the third-party `TiltedCore` library, whose licence ("All Rights Reserved") grants no right
to modify or redistribute it. To avoid touching hundreds of call sites it keeps the same header paths
(`<TiltedCore/Buffer.hpp>`), the `TiltedPhoques` namespace and the same type and function names, so the
rest of the code builds unchanged. Behind those names everything is new.

## Provenance

* Written from how the game's code *uses* the old library, and from the tests in `Code/tests`, not by
  adapting its implementation. The implementation files of the old library were not read.
* While exploring, the *declarations* in a few of its public headers (`Buffer`, `Serialization`, `Stl`,
  `Hash`) were looked at to learn the API shape. Names and signatures are therefore compatible; the code
  behind them is not copied.
* **Wire format.** The bit packing in `Buffer` and the encodings in `Serialization` define what the client
  and server send each other. They are documented in the headers and pinned by tests. They are *not*
  required to match the old library, because client and server are always built together.
* **One external constraint was verified, not assumed.** `FHash::Crc64` must be CRC-64/WE: the animation
  graph descriptors in the game data are keyed by it. The variant was identified by testing candidates
  against a key in `Structs/Skyrim/AnimationGraphDescriptor_Chicken.cpp`, and that key is a test
  (`Code/tests/core.cpp`).

## Behaviour worth knowing

* Allocators: a per-thread current allocator can be swapped with `ScopedAllocator`. Containers and
  `AllocatorCompatible` types tag every block with its owner, so memory made under a scratch allocator
  can safely outlive it.
* Buffers never read or write out of bounds; they return false.
* Serialization readers are written for hostile input: bounded loops, no allocation from an attacker's
  length field.
* `GetPath()` is the directory of the running executable (on Windows via `_get_wpgmptr`). The old
  library's exact rule was not inspected; the launcher uses this, so it is the first thing to check on
  Windows.
