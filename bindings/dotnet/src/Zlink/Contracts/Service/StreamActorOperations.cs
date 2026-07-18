// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Invoked with a request result and its reply parts; the callback owns the
///     parts and must dispose them.
/// </summary>
public delegate void ReplyHandler(RequestResult result,
    IReadOnlyList<Message> parts);

/// <summary>
///     Builds a STREAM session-to-actor bind: optionally set a timeout, then submit.
/// </summary>
public interface ActorBindOperation
{
    /// <summary>Sets how long the operation waits before timing out.</summary>
    ActorBindOperation Timeout(TimeSpan timeout);

    /// <summary>Submits the operation and returns the reply asynchronously.</summary>
    Task<IReadOnlyList<Message>> Async(CancellationToken ct = default);

    /// <summary>Submits the operation; the result is delivered to the callback.</summary>
    bool Submit(ReplyHandler callback);
}

/// <summary>
///     Builds a STREAM session-to-actor unbind: optionally set a timeout, then submit.
/// </summary>
public interface ActorUnbindOperation
{
    /// <summary>Sets how long the operation waits before timing out.</summary>
    ActorUnbindOperation Timeout(TimeSpan timeout);

    /// <summary>Submits the operation and returns the reply asynchronously.</summary>
    Task<IReadOnlyList<Message>> Async(CancellationToken ct = default);

    /// <summary>Submits the operation; the result is delivered to the callback.</summary>
    bool Submit(ReplyHandler callback);
}
