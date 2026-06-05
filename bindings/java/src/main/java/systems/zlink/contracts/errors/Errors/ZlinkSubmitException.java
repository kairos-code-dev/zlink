/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;

import systems.zlink.contracts.sockets.SubmitResult;
/** Thrown when submitting a send or publish fails. */
public final class ZlinkSubmitException extends ZlinkException {
    private final SubmitResult result;

    public ZlinkSubmitException(SubmitResult result) {
        this(result, 0);
    }

    public ZlinkSubmitException(SubmitResult result, int nativeErrno) {
        super(result.value(), nativeErrno);
        this.result = result;
    }

    public SubmitResult getResult() {
        return result;
    }
}
