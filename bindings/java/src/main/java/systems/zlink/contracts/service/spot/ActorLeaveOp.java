/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.Message;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

public interface ActorLeaveOp {
    ActorLeaveOp timeout(Duration timeout);
    CompletableFuture<List<Message>> submitAsync();
    boolean submit(ReplyHandler callback);
}
