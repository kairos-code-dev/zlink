// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IZlinkThread : IDisposable, IAsyncDisposable
{
    void Join();

    void Close();
}
