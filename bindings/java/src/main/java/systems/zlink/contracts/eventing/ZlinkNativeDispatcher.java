/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.sockets.RouterSocket;

/**
 * Poller-backed receive dispatcher for scalable framework integration.
 *
 * <p>The dispatcher owns a small number of runtime threads that wait on native
 * poller readiness and drain registered sockets with {@code DONT_WAIT} recv.
 */
public interface ZlinkNativeDispatcher extends AutoCloseable {
    static ZlinkNativeDispatcher create() {
        return create(ZlinkDispatchOptions.builder().build());
    }

    static ZlinkNativeDispatcher create(ZlinkDispatchOptions options) {
        return new ZlinkPollNativeDispatcher(options);
    }

    ZlinkDispatcherRegistration register(
        PairSocket socket,
        ZlinkReceiveHandler handler);

    ZlinkDispatcherRegistration register(
        DealerSocket socket,
        ZlinkReceiveHandler handler);

    ZlinkDispatcherRegistration register(
        RouterSocket socket,
        ZlinkReceiveHandler handler);

    CompletionStage<Void> start();

    CompletionStage<Void> stop();

    boolean isRunning();

    @Override
    void close();
}
