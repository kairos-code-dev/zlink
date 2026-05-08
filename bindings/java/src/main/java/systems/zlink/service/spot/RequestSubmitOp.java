/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.RequestCallback;
import systems.zlink.SendFlags;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

public interface RequestSubmitOp {
    RequestSubmitOp message(Message part);
    RequestSubmitOp timeout(Duration timeout);
    RequestCallbackSubmitOp flags(SendFlags flags);
    CompletableFuture<List<Message>> submitAsync();
    boolean submit(RequestCallback callback);
}
