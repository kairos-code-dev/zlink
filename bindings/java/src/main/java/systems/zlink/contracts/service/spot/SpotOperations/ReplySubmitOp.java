/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
public interface ReplySubmitOp {
    ReplySubmitOp message(Message part);
    ReplySubmitOp flags(SendFlags flags);
    void submit();
}
