/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlags;

public interface ReplySubmitOp {
    ReplySubmitOp message(Message part);
    ReplySubmitOp flags(SendFlags flags);
    void submit();
}
