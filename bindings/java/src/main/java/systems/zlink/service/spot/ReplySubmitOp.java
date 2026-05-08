/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.SendFlags;

public interface ReplySubmitOp {
    ReplySubmitOp message(Message part);
    ReplySubmitOp flags(SendFlags flags);
    void submit();
}
