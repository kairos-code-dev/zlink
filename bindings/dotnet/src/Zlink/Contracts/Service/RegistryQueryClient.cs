// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IRegistryQueryClient : IDisposable, IAsyncDisposable
{
    void Connect(string endpoint);

    RegistryTopologyEntry[] Snapshot(
        RegistryTopologyFilter? filter = null);

    void Close();
}
