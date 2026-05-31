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

/**
 * A messaging context: the factory and owner of sockets and services.
 *
 * <p>Every socket, registry, discovery, and spot node created here is owned by
 * the caller and must be closed. Closing the context terminates anything still
 * open under it.
 */
public interface Context extends AutoCloseable {
    /** Returns the context-wide options facade. */
    ContextOptions options();

    /** Creates a pair socket; the caller owns it and must close it. */
    PairSocket createPairSocket();

    /** Creates a dealer socket; the caller owns it and must close it. */
    DealerSocket createDealerSocket();

    /** Creates a router socket; the caller owns it and must close it. */
    RouterSocket createRouterSocket();

    /** Creates a pub socket; the caller owns it and must close it. */
    PubSocket createPubSocket();

    /** Creates a sub socket; the caller owns it and must close it. */
    SubSocket createSubSocket();

    /** Creates an xpub socket; the caller owns it and must close it. */
    XPubSocket createXPubSocket();

    /** Creates an xsub socket; the caller owns it and must close it. */
    XSubSocket createXSubSocket();

    /** Creates a stream socket; the caller owns it and must close it. */
    StreamSocket createStreamSocket();

    /** Creates a registry; the caller owns it and must close it. */
    Registry createRegistry();

    /** Creates a registry query client; the caller owns it and must close it. */
    RegistryQueryClient createRegistryQueryClient();

    /**
     * Creates a discovery service for {@code channelName} that connects peers
     * according to {@code autoConnectType}; the caller owns it and must close it.
     *
     * @param autoConnectType how peers are wired together
     * @param channelName the logical channel name
     * @return the discovery service
     */
    Discovery createDiscovery(AutoConnectType autoConnectType,
                              String channelName);

    /** Creates a spot node in the default mode; the caller owns it and must close it. */
    SpotNode createSpotNode();

    /**
     * Creates a spot node in the given mode; the caller owns it and must close it.
     *
     * @param mode which messaging patterns to enable
     * @return the spot node
     */
    SpotNode createSpotNode(SpotNodeMode mode);

    /**
     * Creates a spot node with the given options; the caller owns it and must close it.
     *
     * @param options creation options
     * @return the spot node
     */
    SpotNode createSpotNode(SpotNodeOptions options);

    /**
     * Terminates the context, interrupting blocking operations on its sockets
     * without closing them.
     */
    void shutdown();

    /**
     * Recomputes automatic high-water marks for sockets configured with an
     * {@link systems.zlink.contracts.sockets.AutoHwmProfile}.
     */
    void recalculateAutoHwm();

    @Override
    void close();
}
