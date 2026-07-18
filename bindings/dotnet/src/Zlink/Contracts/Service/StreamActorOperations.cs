// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Invoked with a request result and its reply parts; the callback owns the
///     parts and must dispose them.
/// </summary>
public delegate void ReplyHandler(RequestResult result,
    IReadOnlyList<Message> parts);
