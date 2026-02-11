[English](testing.md) | [한국어](testing.ko.md)

# Testing Strategy and Execution

## 1. Test Framework

zlink uses **Unity** (a C-based unit testing framework).

## 2. Running Tests

### All Tests
```bash
ctest --test-dir build --output-on-failure
```

### Specific Test
```bash
ctest --test-dir build -R test_pair --output-on-failure
```

## 3. Test Directory Structure

| Directory | Purpose |
|-----------|---------|
| `core/tests/` | Functional tests (user-facing behavior verification) |
| `core/unittests/` | Internal logic tests |

### Test Organization by Feature
```
tests/
├── routing-id/
│   └── test_router_auto_id_format.cpp
├── monitoring/
│   └── test_monitor_enhanced.cpp
├── discovery/
│   └── test_service_discovery.cpp
└── spot/
    └── test_spot_pubsub_basic.cpp
```

## 4. Test Writing Guide

- Add to `tests/` for behavior changes
- Add to `unittests/` for internal changes
- Note any platform-specific skip conditions in the PR

## 5. Binding Integration Tests

### 5.1 Common Scenarios

| Scenario | Description |
|----------|-------------|
| S1 | Context/Socket Lifecycle |
| S2 | PAIR basic roundtrip |
| S3 | PUB/SUB basic |
| S4 | DEALER/ROUTER basic |
| S5 | XPUB/XSUB subscription propagation |
| S6 | Message multipart |
| S7 | Socket options |
| S8 | Registry/Discovery |
| S9 | Gateway |
| S10 | Spot |

### 5.2 Transport Loop

All scenarios are executed across 3 transports:
- `tcp://127.0.0.1:PORT`
- `ws://127.0.0.1:PORT`
- `inproc://name`

### 5.3 Test Locations by Language

| Language | Framework | Location |
|----------|-----------|----------|
| .NET | xUnit | `bindings/dotnet/tests/` |
| Java | JUnit | `bindings/java/src/test/` |
| Node.js | node:test | `bindings/node/tests/` |
| Python | unittest | `bindings/python/tests/` |
