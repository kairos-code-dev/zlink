// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IContext : IDisposable, IAsyncDisposable
{
    IContextOptions Options { get; }

    void Shutdown();

    void RecalculateAutoHwm();
}
