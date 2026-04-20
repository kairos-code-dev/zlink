// SPDX-License-Identifier: MPL-2.0

// Package zlink provides idiomatic Go bindings for the zlink messaging library.
//
// The public surface follows the shared bindings policy in bindings/README.md:
//   - multipart-only public send/receive APIs
//   - blocking methods use direct names and non-blocking methods use Try*
//   - non-blocking submit returns `(false, nil)` only for temporary backpressure
//   - non-blocking receive reports only "no data" via the ok result
//   - message diagnostics expose GetProperty and RefCount
//   - context options are exposed through Context.Options() as ContextOptions
//   - socket capabilities are split by concrete socket type
//   - typed domain objects model routing IDs, multipart receive results, topic
//     messages, subscription events, and monitor events
//   - raw option bags and raw flags are not exposed publicly
//   - service monitor entry points are discovery-only
//   - monitor open APIs use typed event masks and default to ALL when omitted
//   - callback delivery hops from native threads onto Go-managed dispatcher
//     goroutines before invoking user handlers
//
// Message ownership follows the native contract. Successful send paths consume
// message ownership, while receive paths transfer ownership to Go wrappers that
// must be closed explicitly when their lifetime ends.
package zlink
