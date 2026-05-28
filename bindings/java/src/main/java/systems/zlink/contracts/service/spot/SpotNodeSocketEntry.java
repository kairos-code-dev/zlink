/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.eventing.MonitorStatus;
import systems.zlink.contracts.sockets.SocketType;

public record SpotNodeSocketEntry(
    SpotNodeSocketOwner owner,
    long ownerId,
    String ownerName,
    String socketName,
    SocketType socketType,
    boolean autoHwmVisible,
    MonitorStatus monitorStatus) {
}
