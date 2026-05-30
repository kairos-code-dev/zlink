// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// A read-only client that queries a registry for its topology.
/// </summary>
public interface IRegistryQueryClient : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Connects to a registry's query endpoint.
    /// </summary>
    void Connect(string endpoint);

    /// <summary>
    /// Returns the registry's topology entries, optionally filtered. The caller
    /// owns the returned array.
    /// </summary>
    RegistryTopologyEntry[] Topology(
        RegistryTopologyFilter? filter = null);

    /// <summary>
    /// Closes the query client and releases its resources.
    /// </summary>
    void Close();
}
