/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RequestCallback;
import dev.kairoscode.zlink.SendFlags;
import java.time.Duration;

public interface RequestCallbackSubmitOp {
    RequestCallbackSubmitOp message(Message part);
    RequestCallbackSubmitOp timeout(Duration timeout);
    RequestCallbackSubmitOp flags(SendFlags flags);
    boolean submit(RequestCallback callback);
}
