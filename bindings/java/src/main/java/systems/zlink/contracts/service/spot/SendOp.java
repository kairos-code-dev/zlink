/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.Message;

public interface SendOp {
    SendSubmitOp message(Message part);
}
