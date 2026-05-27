// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IPoller : IDisposable, IAsyncDisposable
{
    int Size { get; }

    void Add(IZlinkSocket socket, PollEventFlags events, nuint slot);

    void AddFd(int fd, PollEventFlags events, nuint slot);

    void Add(IZlinkTimer timer, nuint slot);

    void Modify(IZlinkSocket socket, PollEventFlags events);

    void ModifyFd(int fd, PollEventFlags events);

    bool Remove(IZlinkSocket socket);

    bool Remove(IZlinkTimer timer);

    bool Remove(int fd);

    void Clear();

    void Close();

    int Wait(Span<PollEvent> destination, TimeSpan timeout);
}
