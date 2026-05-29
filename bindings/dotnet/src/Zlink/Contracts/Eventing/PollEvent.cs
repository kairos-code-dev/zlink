// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Represents poll event.
/// </summary>
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

    /// <summary>
    /// Gets the source kind.
    /// </summary>
    public PollSourceKind SourceKind { get; }
    /// <summary>
    /// Gets the slot.
    /// </summary>
    public nuint Slot { get; }
    /// <summary>
    /// Gets the returned poll events.
    /// </summary>
    public PollEventFlags Revents { get; }
    /// <summary>
    /// Gets the file descriptor.
    /// </summary>
    public int Fd { get; }
}
