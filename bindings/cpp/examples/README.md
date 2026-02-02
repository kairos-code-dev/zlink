# C++ Binding Examples

## Build
Use the root build with C++ bindings enabled:

```bash
cmake -B build -DZLINK_BUILD_CPP_BINDINGS=ON
cmake --build build
```

## Examples
- `pair_basic.cpp` : PAIR basic send/recv
- `dealer_router.cpp` : DEALER/ROUTER roundtrip
- `spot_basic.cpp` : SPOT local publish/recv
