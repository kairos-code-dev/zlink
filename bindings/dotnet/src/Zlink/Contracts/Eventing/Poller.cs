// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the poller contract.
/// </summary>
public interface IPoller : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets or sets the size.
    /// </summary>
    int Size { get; }

    /// <summary>
    /// Adds the value.
    /// </summary>
    void Add(IZlinkSocket socket, PollEventFlags events, nuint slot);

    /// <summary>
    /// Adds the fd.
    /// </summary>
    void AddFd(int fd, PollEventFlags events, nuint slot);

    /// <summary>
    /// Adds the value.
    /// </summary>
    void Add(IZlinkTimer timer, nuint slot);

    /// <summary>
    /// Gets or sets the modify.
    /// </summary>
    void Modify(IZlinkSocket socket, PollEventFlags events);

    /// <summary>
    /// Gets or sets the modify fd.
    /// </summary>
    void ModifyFd(int fd, PollEventFlags events);

    /// <summary>
    /// Removes the value.
    /// </summary>
    bool Remove(IZlinkSocket socket);

    /// <summary>
    /// Removes the value.
    /// </summary>
    bool Remove(IZlinkTimer timer);

    /// <summary>
    /// Removes the value.
    /// </summary>
    bool Remove(int fd);

    /// <summary>
    /// Clears the current state.
    /// </summary>
    void Clear();

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();

    /// <summary>
    /// Gets or sets the wait.
    /// </summary>
    int Wait(Span<PollEvent> destination, TimeSpan timeout);
}
