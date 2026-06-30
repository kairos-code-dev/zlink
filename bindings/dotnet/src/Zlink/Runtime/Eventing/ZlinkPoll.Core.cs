// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public static partial class ZlinkPoll
{
    private static int PollCore(IReadOnlyList<IZlinkSocket> sockets,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(sockets, timeoutMs);
    }

    private static int PollCore(IReadOnlyList<IZlinkSocket> sockets,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(sockets, events, revents, timeoutMs);
    }

    private static int PollCore(IReadOnlyList<ISocketMonitor> monitors,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(monitors, timeoutMs);
    }

    private static int PollCore(IReadOnlyList<ISocketMonitor> monitors,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        return ZlinkPollRuntime.Poll(monitors, events, revents, timeoutMs);
    }
}