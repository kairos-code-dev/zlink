// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

public static class ZlinkPoll
{
    public static int Poll(IReadOnlyList<IZlinkSocket> sockets, int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(sockets, timeoutMs);
    }

    public static int Poll(IReadOnlyList<IZlinkSocket> sockets,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(sockets, events, revents, timeoutMs);
    }

    public static int Poll(IReadOnlyList<ISocketMonitor> monitors,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(monitors, timeoutMs);
    }

    public static int Poll(IReadOnlyList<ISocketMonitor> monitors,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(monitors, events, revents, timeoutMs);
    }
}
