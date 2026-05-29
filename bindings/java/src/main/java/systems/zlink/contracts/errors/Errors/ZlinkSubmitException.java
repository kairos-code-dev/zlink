/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;

import systems.zlink.contracts.sockets.SubmitResult;
public final class ZlinkSubmitException extends ZlinkException {
    private final SubmitResult result;

    public ZlinkSubmitException(SubmitResult result) {
        this(result, 0);
    }

    public ZlinkSubmitException(SubmitResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public SubmitResult getResult() {
        return result;
    }
}
