# inproc Performance Optimization - Issue & Background

**Status:** Phase 1 applied (mailbox_t refactor). Residual gap remains vs libzmq.

## Problem

- inproc throughput is consistently below libzmq in 10K/64B bench runs.
- Gap is typically ~10-20% depending on pattern and run variance.

## Primary Suspect

- zlink uses a condition_variable-based mailbox_t with recv-side locking.
- libzmq uses a signaler-based mailbox_t and lock-free recv path.
- inproc relies on mailbox_t for activate_read/write command traffic across threads.

## Investigation Notes

- pipe/ypipe implementation matches expected upstream behavior.
- The main behavioral divergence found in mailbox_t.
- Focused on restoring upstream mailbox_t semantics while keeping ASIO integration.
