// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public readonly struct PollEvent
{
    internal PollEvent(PollSourceKind sourceKind, nuint slot,
        PollEventFlags revents, int fd)
    {
        SourceKind = sourceKind;
        Slot = slot;
        Revents = revents;
        Fd = fd;
    }

    public PollSourceKind SourceKind { get; }
    public nuint Slot { get; }
    public PollEventFlags Revents { get; }
    public int Fd { get; }
}
