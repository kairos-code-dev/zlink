// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

/// <summary>
/// Represents zlink poll.
/// </summary>
public static class ZlinkPoll
{
    /// <summary>
    /// Waits for poll events.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static int Poll(IReadOnlyList<IZlinkSocket> sockets, int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(sockets, timeoutMs);
    }

    /// <summary>
    /// Waits for poll events.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static int Poll(IReadOnlyList<IZlinkSocket> sockets,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(sockets, events, revents, timeoutMs);
    }

    /// <summary>
    /// Waits for poll events.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static int Poll(IReadOnlyList<ISocketMonitor> monitors,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(monitors, timeoutMs);
    }

    /// <summary>
    /// Waits for poll events.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static int Poll(IReadOnlyList<ISocketMonitor> monitors,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(monitors, events, revents, timeoutMs);
    }
}
