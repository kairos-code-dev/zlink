/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;

public interface ReplyOp {
    ReplySubmitOp message(Message part);
}
