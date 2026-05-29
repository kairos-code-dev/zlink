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
    /// Gets or sets the source kind.
    /// </summary>
    public PollSourceKind SourceKind { get; }
    /// <summary>
    /// Gets or sets the slot.
    /// </summary>
    public nuint Slot { get; }
    /// <summary>
    /// Gets or sets the revents.
    /// </summary>
    public PollEventFlags Revents { get; }
    /// <summary>
    /// Gets or sets the fd.
    /// </summary>
    public int Fd { get; }
}
