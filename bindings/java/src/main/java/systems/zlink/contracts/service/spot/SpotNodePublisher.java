/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Publishes into the local SPOT topic plane without attaching an external PUB socket. */
public interface SpotNodePublisher extends AutoCloseable {
    SendOperation publish(String topicId);

    void close();
}
