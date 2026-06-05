/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


/** Thrown when closing a socket or resource fails. */
public final class ZlinkCloseException extends ZlinkException {
    private final CloseResult result;

    public ZlinkCloseException(CloseResult result) {
        this(result, 0);
    }

    public ZlinkCloseException(CloseResult result, int nativeErrno) {
        super(result.value(), nativeErrno);
        this.result = result;
    }

    public CloseResult getResult() {
        return result;
    }
}
