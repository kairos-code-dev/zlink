/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.core;

import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.service.registry.RegistryQueryClient;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeMode;
import systems.zlink.contracts.service.spot.SpotNodeOptions;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.sockets.PubSocket;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.contracts.sockets.SubSocket;
import systems.zlink.contracts.sockets.XPubSocket;
import systems.zlink.contracts.sockets.XSubSocket;

public interface Context extends AutoCloseable {
    ContextOptions options();

    PairSocket createPairSocket();

    DealerSocket createDealerSocket();

    RouterSocket createRouterSocket();

    PubSocket createPubSocket();

    SubSocket createSubSocket();

    XPubSocket createXPubSocket();

    XSubSocket createXSubSocket();

    StreamSocket createStreamSocket();

    Registry createRegistry();

    RegistryQueryClient createRegistryQueryClient();

    Discovery createDiscovery(AutoConnectType autoConnectType,
                              String channelName);

    SpotNode createSpotNode();

    SpotNode createSpotNode(SpotNodeMode mode);

    SpotNode createSpotNode(SpotNodeOptions options);

    void shutdown();

    void recalculateAutoHwm();

    @Override
    void close();
}
