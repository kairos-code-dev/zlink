/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;

import systems.zlink.contracts.sockets.RecvResult;
public final class ZlinkRecvException extends ZlinkException {
    private final RecvResult result;

    public ZlinkRecvException(RecvResult result) {
        this(result, 0);
    }

    public ZlinkRecvException(RecvResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public RecvResult getResult() {
        return result;
    }
}
