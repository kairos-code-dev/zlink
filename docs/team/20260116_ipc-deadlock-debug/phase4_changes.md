# Phase 4 Changes

## Overview
- Wrapped timer and handshake handlers with `boost::asio::bind_executor` to ensure strand serialization.
- Switched `restart_input` scheduling from `dispatch` to `post` to avoid immediate inline execution.

## Code Changes
- Timer handler now uses `boost::asio::bind_executor(*_strand, ...)` in `add_timer`.
- Transport handshake handler now uses `boost::asio::bind_executor(*_strand, ...)` in `start_transport_handshake`.
- `restart_input` now uses `boost::asio::post(*_strand, ...)` instead of `dispatch`.

## Files Modified
- `src/asio/asio_engine.cpp`
- `docs/team/20260116_ipc-deadlock-debug/phase4_changes.md`
