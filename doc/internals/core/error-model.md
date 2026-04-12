# Core Error Model

This document explains how core keeps detailed internal errors while exposing
stable public results at the API boundary.

## Layers

- Internal execution paths keep using `int errno`.
- Exported C APIs normalize submit failures to `zlink_submit_result_t`.
- Request reply callbacks still pass raw `errno_`, but that completion channel
  is normalized by contract as `zlink_request_result_t`.

The code is organized around three files:

- [core/include/zlink_errno.h](/home/hep7/project/kairos/zlink/core/include/zlink_errno.h)
  defines public extended errno values.
- [core/include/zlink_enum.h](/home/hep7/project/kairos/zlink/core/include/zlink_enum.h)
  defines public result enums.
- [core/src/core/internal_errno.hpp](/home/hep7/project/kairos/zlink/core/src/core/internal_errno.hpp)
  defines the internal errno catalog used by normalization helpers.

## Why Internal `errno` Stays

Core still interacts with OS and protocol code that naturally reports failure
through `errno`. Keeping that detailed channel avoids losing information inside
the implementation.

Public callers do not need that full detail. They need stable result classes.
That is why normalization happens only at the exported boundary.

## Implementation Rule

Inside core and benchmark/test helper code, public result enums must be treated
as named result codes, not as booleans.

- Use `rc == ZLINK_*_OK` or `rc != ZLINK_*_OK`.
- Do not write boolean-style checks such as `if (!zlink_bind(...))`.

That rule matters because all public result enums use `0` for success. Boolean
style can silently invert success and failure, which is exactly the kind of bug
the typed-result policy is meant to prevent.

## Submit Normalization

Send, request submit, and reply submit share one public result type:
`zlink_submit_result_t`.

The normalization helper lives in
[core/src/api/submit_result_internal.hpp](/home/hep7/project/kairos/zlink/core/src/api/submit_result_internal.hpp).
It maps the internal submit errno catalog to public submit results.

## Request Completion Normalization

Request completion uses a separate public result type:
`zlink_request_result_t`.

The normalization helper lives in
[core/src/api/request_result_internal.hpp](/home/hep7/project/kairos/zlink/core/src/api/request_result_internal.hpp).
It maps callback completion errno values to the public completion result
contract.
