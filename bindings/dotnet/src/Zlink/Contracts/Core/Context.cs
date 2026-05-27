// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IContext : IDisposable, IAsyncDisposable
{
    IContextOptions Options { get; }

    IPairSocket CreatePairSocket();

    IDealerSocket CreateDealerSocket();

    IRouterSocket CreateRouterSocket();

    IPubSocket CreatePubSocket();

    ISubSocket CreateSubSocket();

    IXPubSocket CreateXPubSocket();

    IXSubSocket CreateXSubSocket();

    IStreamSocket CreateStreamSocket();

    IRegistry CreateRegistry();

    IRegistryQueryClient CreateRegistryQueryClient();

    IDiscovery CreateDiscovery(AutoConnectType autoConnectType,
        string channelName);

    ISpotNode CreateSpotNode();

    ISpotNode CreateSpotNode(SpotNodeMode mode);

    void Shutdown();

    void RecalculateAutoHwm();
}
