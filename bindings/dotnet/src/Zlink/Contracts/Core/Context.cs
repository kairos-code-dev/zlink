// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the context contract.
/// </summary>
public interface IContext : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets or sets the options.
    /// </summary>
    IContextOptions Options { get; }

    /// <summary>
    /// Creates a pair socket.
    /// </summary>
    IPairSocket CreatePairSocket();

    /// <summary>
    /// Creates a dealer socket.
    /// </summary>
    IDealerSocket CreateDealerSocket();

    /// <summary>
    /// Creates a router socket.
    /// </summary>
    IRouterSocket CreateRouterSocket();

    /// <summary>
    /// Creates a pub socket.
    /// </summary>
    IPubSocket CreatePubSocket();

    /// <summary>
    /// Creates a sub socket.
    /// </summary>
    ISubSocket CreateSubSocket();

    /// <summary>
    /// Creates a xpub socket.
    /// </summary>
    IXPubSocket CreateXPubSocket();

    /// <summary>
    /// Creates a xsub socket.
    /// </summary>
    IXSubSocket CreateXSubSocket();

    /// <summary>
    /// Creates a stream socket.
    /// </summary>
    IStreamSocket CreateStreamSocket();

    /// <summary>
    /// Creates a registry.
    /// </summary>
    IRegistry CreateRegistry();

    /// <summary>
    /// Creates a registry query client.
    /// </summary>
    IRegistryQueryClient CreateRegistryQueryClient();

    /// <summary>
    /// Creates a discovery.
    /// </summary>
    IDiscovery CreateDiscovery(AutoConnectType autoConnectType,
        string channelName);

    /// <summary>
    /// Creates a spot node.
    /// </summary>
    ISpotNode CreateSpotNode();

    /// <summary>
    /// Creates a spot node.
    /// </summary>
    ISpotNode CreateSpotNode(SpotNodeMode mode);

    /// <summary>
    /// Gets or sets the shutdown.
    /// </summary>
    void Shutdown();

    /// <summary>
    /// Recalculates automatic high water marks.
    /// </summary>
    void RecalculateAutoHwm();
}
