// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IZlinkTimer : IDisposable, IAsyncDisposable
{
    void Start(TimeSpan interval, ulong repeatCount);

    void Stop();

    ulong? Recv(RecvFlags flags = RecvFlags.None);

    void OnFire(Action<IZlinkTimer, ulong> handler);

    void Close();
}
