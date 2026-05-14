// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IPoller : IDisposable, IAsyncDisposable
{
    int Size { get; }

    void Add(IZlinkSocket socket, PollEventFlags events, object? tag = null);

    void AddFd(int fd, PollEventFlags events, object? tag = null);

    void Add(IZlinkTimer timer, object? tag = null);

    void Modify(IZlinkSocket socket, PollEventFlags events);

    void ModifyFd(int fd, PollEventFlags events);

    bool Remove(IZlinkSocket socket);

    bool Remove(IZlinkTimer timer);

    bool Remove(int fd);

    void Clear();

    void Close();

    PollEvent? Wait(TimeSpan timeout);

    int Wait(Span<PollEvent> destination, TimeSpan timeout,
        out int totalReady);
}
