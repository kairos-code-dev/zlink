/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;

import systems.zlink.contracts.sockets.RecvResult;
/** Thrown when receiving a message fails. */
public final class ZlinkRecvException extends ZlinkException {
    private final RecvResult result;

    public ZlinkRecvException(RecvResult result) {
        this(result, 0);
    }

    public ZlinkRecvException(RecvResult result, int nativeErrno) {
        super(result.value(), nativeErrno);
        this.result = result;
    }

    public RecvResult getResult() {
        return result;
    }
}
