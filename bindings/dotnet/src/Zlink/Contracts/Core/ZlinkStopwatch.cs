// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IZlinkStopwatch : IDisposable, IAsyncDisposable
{
    ulong Intermediate();

    ulong Stop();
}
