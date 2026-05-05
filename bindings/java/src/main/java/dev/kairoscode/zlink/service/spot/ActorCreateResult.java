/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import java.util.Objects;

public record ActorCreateResult(ActorCreateStatus status, ActorRef actor) {
    public ActorCreateResult {
        Objects.requireNonNull(status, "status");
        Objects.requireNonNull(actor, "actor");
    }
}
