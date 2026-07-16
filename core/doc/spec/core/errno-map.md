[한국어](errno-map.ko.md) | English

[Specification index](../README.md) · [Core index](README.md) · [Errors and result enums](errors.md)

# Result and errno mapping

This document defines result-enum and thread-local errno mappings for the ZLink Core 10.0.0 public API. Its audience is developers who need stable error classification in the C API and bindings. Results drive control flow; errno describes the same failure in more detail.

## 1. Common precedence

When multiple failure conditions overlap, one result is selected in this order:

1. argument errors such as null pointers, invalid sizes or enum values, and malformed metadata;
2. handle-kind and lifecycle-state errors;
3. target, generation, binding, and connection lookup errors;
4. admission, capacity, and backpressure errors;
5. transport and internal runtime errors.

Errno is unspecified after success. Where a row lists multiple errno values, the owner document distinguishes their conditions.

## 2. Submit result

| Result | errno | Meaning |
|---|---|---|
| `ZLINK_SUBMIT_OK` | - | The function’s ownership transition completed |
| `ZLINK_SUBMIT_BACKPRESSURED` | `EAGAIN`, `ETIMEDOUT`, `ENOBUFS` | Queue or reservation capacity is unavailable |
| `ZLINK_SUBMIT_NOT_CONNECTED` | `ENOTCONN` | No target pipe or session connection |
| `ZLINK_SUBMIT_NOT_FOUND` | `ENOENT` | No channel member, Spot, Actor, or binding |
| `ZLINK_SUBMIT_NOT_ADMITTED` | `EACCES` | Peer or destination has not passed admission |
| `ZLINK_SUBMIT_TERMINATED` | `ETERM`, `ESHUTDOWN` | Context or service lifecycle ended |
| `ZLINK_SUBMIT_INVALID_HANDLE` | `EFAULT` | Handle is null or has the wrong kind |
| `ZLINK_SUBMIT_INVALID_ARGUMENT` | `EINVAL`, `EMSGSIZE` | Invalid pointer, count, name, metadata, or flags |
| `ZLINK_SUBMIT_NOT_SUPPORTED` | `ENOTSUP` | Operation is unsupported by the handle |
| `ZLINK_SUBMIT_INVALID_STATE` | `EBUSY`, `ESTALE`, `EALREADY`, `ESHUTDOWN` | Invalid lifecycle, generation, token, or binding state |
| `ZLINK_SUBMIT_THREAD_VIOLATION` | `EDEADLK`, `EPERM` | Forbidden callback reentry or thread use |
| `ZLINK_SUBMIT_OUT_OF_MEMORY` | `ENOMEM` | Required storage cannot be acquired |
| `ZLINK_SUBMIT_SEQ_EXHAUSTED` | `EOVERFLOW` | Operation-sequence space is exhausted |
| `ZLINK_SUBMIT_INTERNAL_ERROR` | preserved errno | Internal failure without a finer public category |

These results apply to raw socket send and request submit, MeshNode node/channel/publisher operations, Spot direct/channel/publish operations, Actor messaging, and STREAM-session operations. Service complete-multipart inputs remain borrowed on both success and failure. Each socket document defines raw-socket input ownership.

## 3. Request completion result

| Result | errno | Meaning |
|---|---|---|
| `ZLINK_REQUEST_OK` | - | Terminal success |
| `ZLINK_REQUEST_TIMED_OUT` | `ETIMEDOUT` | Operation or shutdown deadline expired |
| `ZLINK_REQUEST_NOT_FOUND` | `ENOENT` | Terminal target absence |
| `ZLINK_REQUEST_TERMINATED` | `ETERM`, `ESHUTDOWN` | Owner lifecycle ended |
| `ZLINK_REQUEST_PROTOCOL_ERROR` | `EPROTO`, `ENOCOMPATPROTO` | Malformed or incompatible reply |
| `ZLINK_REQUEST_INTERNAL_ERROR` | preserved errno | Internal failure without a finer terminal category |
| `ZLINK_REQUEST_REJECTED` | `EACCES`, `ECANCELED` | Application or admission rejection |
| `ZLINK_REQUEST_CONFLICT` | `EEXIST`, `ESTALE` | Compare-and-set or generation conflict |
| `ZLINK_REQUEST_BUSY` | `EBUSY` | Active claim, binding, or lifecycle operation |
| `ZLINK_REQUEST_NOT_CONNECTED` | `ENOTCONN` | Terminal route loss |
| `ZLINK_REQUEST_INVALID_ARGUMENT` | `EINVAL` | Asynchronous validation failure |
| `ZLINK_REQUEST_INVALID_STATE` | `ESTALE`, `EALREADY`, `ESHUTDOWN` | Terminal token or lifecycle-state error |
| `ZLINK_REQUEST_NOT_SUPPORTED` | `ENOTSUP` | Unsupported operation |
| `ZLINK_REQUEST_BACKPRESSURED` | `ENOBUFS` | Atomic whole-capacity reservation failed |

After successful request submission, exactly one terminal result is delivered in a completion record for each operation ID. Synchronous shutdown and transfer prepare also return this enum.

## 4. Receive result

| Result | errno | Meaning |
|---|---|---|
| `ZLINK_RECV_OK` | - | At least one complete record was received |
| `ZLINK_RECV_NO_DATA` | `EAGAIN`, `ETIMEDOUT` | No data under nonblocking receive or receive timeout |
| `ZLINK_RECV_BUSY` | `EBUSY` | Another receive mode or the same mutable batch is active |
| `ZLINK_RECV_TERMINATED` | `ETERM` | Context terminated |
| `ZLINK_RECV_INVALID_HANDLE` | `EFAULT` | Invalid handle or batch |
| `ZLINK_RECV_NOT_SUPPORTED` | `ENOTSUP` | Handle does not support the receive operation |
| `ZLINK_RECV_INTERNAL_ERROR` | preserved errno | Internal failure without a finer public category |
| `ZLINK_RECV_BUFFER_TOO_SMALL` | `ENOBUFS` | Batch cannot hold the first complete record |
| `ZLINK_RECV_INVALID_STATE` | `EINVAL`, `ESTALE`, `ESHUTDOWN` | Invalid claim domain, generation, or revoke state |

On `BUFFER_TOO_SMALL`, the batch is empty and the required output reports message, part, and byte capacity.

## 5. Handler and close results

| Result | errno | Meaning |
|---|---|---|
| `ZLINK_HANDLER_INVALID_ARGUMENT` | `EINVAL` | Invalid handler or mask |
| `ZLINK_HANDLER_BUSY` | `EBUSY` | An exclusive receive model is already registered |
| `ZLINK_HANDLER_NOT_SUPPORTED` | `ENOTSUP` | Handle does not support the handler |
| `ZLINK_HANDLER_DEADLOCK` | `EDEADLK` | Forbidden registration or removal inside the same callback |
| `ZLINK_HANDLER_INVALID_HANDLE` | `EFAULT` | Invalid handle |
| `ZLINK_HANDLER_INTERNAL_ERROR` | preserved errno | Internal failure without a finer public category |
| `ZLINK_CLOSE_BUSY` | `EBUSY` | Active child handle, callback, or API exists |
| `ZLINK_CLOSE_SHUTDOWN` | `ESHUTDOWN` | Handle is already stopped |
| `ZLINK_CLOSE_INVALID_HANDLE` | `EFAULT`, `ESTALE` | Invalid pointer or opaque value |
| `ZLINK_CLOSE_INTERNAL_ERROR` | preserved errno | Internal failure without a finer public category |

## 6. Bind and connect results

| Result | errno | Meaning |
|---|---|---|
| `ZLINK_BIND_INVALID_ARGUMENT` | `EINVAL` | Invalid endpoint |
| `ZLINK_BIND_ADDR_IN_USE` | `EADDRINUSE` | Endpoint is already in use |
| `ZLINK_BIND_NOT_SUPPORTED` | `ENOTSUP`, `EPROTONOSUPPORT` | Unsupported transport |
| `ZLINK_BIND_INVALID_HANDLE` | `EFAULT` | Invalid handle |
| `ZLINK_BIND_INTERNAL_ERROR` | preserved errno | Bind failure without a finer public category |
| `ZLINK_CONNECT_INVALID_ARGUMENT` | `EINVAL` | Invalid endpoint or expected RID |
| `ZLINK_CONNECT_NOT_SUPPORTED` | `ENOTSUP`, `EPROTONOSUPPORT` | Unsupported transport or operation |
| `ZLINK_CONNECT_INVALID_HANDLE` | `EFAULT` | Invalid handle |
| `ZLINK_CONNECT_INTERNAL_ERROR` | preserved errno | Connect failure without a finer public category |
| `ZLINK_CONNECT_NOT_FOUND` | `ENOENT` | Intent or peer does not exist |
| `ZLINK_CONNECT_CONFLICT` | `EEXIST`, `ESTALE`, `EADDRINUSE` | MeshName, RID, or generation conflict |
| `ZLINK_CONNECT_BUSY` | `EBUSY`, `ESHUTDOWN` | Lifecycle rejects the change |
| `ZLINK_CONNECT_AUTH_FAILED` | `EACCES` | Trust-profile or peer-authentication failure |

## 7. Configuration result

| Result | errno | Meaning |
|---|---|---|
| `ZLINK_CONFIG_INVALID_HANDLE` | `EFAULT` | Invalid handle or output pointer |
| `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL`, `EMSGSIZE` | Invalid option, size, name, or value |
| `ZLINK_CONFIG_NOT_SUPPORTED` | `ENOTSUP` | Unsupported handle and option combination |
| `ZLINK_CONFIG_INTERNAL_ERROR` | preserved errno | Internal failure without a finer public category |
| `ZLINK_CONFIG_INVALID_STATE` | `EINVAL`, `EBUSY`, `ESTALE`, `EALREADY`, `ESHUTDOWN` | Lifecycle phase, generation, or terminal state rejects the change |
| `ZLINK_CONFIG_NOT_FOUND` | `ENOENT` | Local query target does not exist |
| `ZLINK_CONFIG_CONFLICT` | `EEXIST` | Duplicate identity, name, or binding |
| `ZLINK_CONFIG_BUFFER_TOO_SMALL` | `ENOBUFS` | Caller output capacity is insufficient; no partial output |
| `ZLINK_CONFIG_BUSY` | `EBUSY` | Same mutable object is in concurrent use |

## 8. Service function families

| Family | Submit and request owner | Receive and configuration owner |
|---|---|---|
| MeshNode | `zlink_mesh_node_send_*`, `request_*`, `publisher_publish` | Lifecycle, peers, status, and publisher options |
| Dispatch | `zlink_mesh_reply` | Ready and receive batches, claims, and handlers |
| Spot | `zlink_spot_send_*`, `request_*`, `publish` | Spot lifecycle, subscriptions, timer, and publish options |
| Actor | Actor creation, join, leave, messaging, and transfer | Actor lookup, claims, and control records |
| STREAM session | Bind, unbind, send, request, and close | Service lifecycle, binding queries, and status |

The formal service document for each family owns exact arguments, ownership, and lifecycle conditions.
