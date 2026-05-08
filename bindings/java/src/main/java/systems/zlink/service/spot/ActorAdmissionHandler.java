/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;

@FunctionalInterface
public interface ActorAdmissionHandler {
    ActorAdmissionResult onActorAdmission(String actorId, Message message);
}
