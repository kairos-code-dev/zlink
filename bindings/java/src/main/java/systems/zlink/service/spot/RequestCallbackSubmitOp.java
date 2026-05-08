/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.RequestCallback;
import systems.zlink.SendFlags;
import java.time.Duration;

public interface RequestCallbackSubmitOp {
    RequestCallbackSubmitOp message(Message part);
    RequestCallbackSubmitOp timeout(Duration timeout);
    RequestCallbackSubmitOp flags(SendFlags flags);
    boolean submit(RequestCallback callback);
}
