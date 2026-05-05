/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;

@FunctionalInterface
public interface ActorAdmissionHandler {
    ActorAdmissionResult onActorAdmission(String actorId, Message message);
}
