// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the registry query client contract.
/// </summary>
public interface IRegistryQueryClient : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Connects to the endpoint.
    /// </summary>
    void Connect(string endpoint);

    /// <summary>
    /// Converts the value to pology.
    /// </summary>
    RegistryTopologyEntry[] Topology(
        RegistryTopologyFilter? filter = null);

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}
