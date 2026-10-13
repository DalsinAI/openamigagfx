# A host NDK, for testing only

Just enough of the AmigaOS 3.2 interface for `tests/m68k/test_layers_lib.sh` to build OpenLayers' library code on Linux and under qemu-m68k: the structures OpenLayers touches, with the published field names and offsets (checked under qemu-m68k, where alignment is the Amiga's), and the exec and graphics calls it makes, which the test supplies in C over a pixel model. Clean-room, written from the published layouts; no NDK text. The real NDK 3.2 builds the library itself (`build-amiga.sh`).
