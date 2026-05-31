/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.eventing.MonitorStatus;
import systems.zlink.contracts.sockets.SocketType;

/**
 * One socket owned within a spot node and its monitored status.
 * @param owner which component owns the socket
 * @param ownerId the identifier of the owning component
 * @param ownerName the name of the owning component
 * @param socketName the socket's name
 * @param socketType the socket's type
 * @param autoHwmVisible whether the socket participates in auto-HWM sizing
 * @param monitorStatus the socket's monitored status
 */
public record SpotNodeSocketEntry(
    SpotNodeSocketOwner owner,
    long ownerId,
    String ownerName,
    String socketName,
    SocketType socketType,
    boolean autoHwmVisible,
    MonitorStatus monitorStatus) {
}
