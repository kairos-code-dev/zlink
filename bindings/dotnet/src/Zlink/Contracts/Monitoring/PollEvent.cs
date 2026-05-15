// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public readonly struct PollEvent
{
    internal PollEvent(IZlinkSocket? socket, int? fd, IZlinkTimer? timer,
        object? tag, PollEventFlags events, PollEventFlags revents)
    {
        Socket = socket;
        Fd = fd;
        Timer = timer;
        Tag = tag;
        Events = events;
        Revents = revents;
    }

    public IZlinkSocket? Socket { get; }
    public int? Fd { get; }
    public IZlinkTimer? Timer { get; }
    public object? Tag { get; }
    public PollEventFlags Events { get; }
    public PollEventFlags Revents { get; }
}
