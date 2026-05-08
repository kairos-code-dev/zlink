/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RequestCallback;
import dev.kairoscode.zlink.SendFlags;
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
