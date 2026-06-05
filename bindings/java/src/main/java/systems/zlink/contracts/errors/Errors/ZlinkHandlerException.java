/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


/** Thrown when registering or running a callback handler fails. */
public final class ZlinkHandlerException extends ZlinkException {
    private final HandlerResult result;

    public ZlinkHandlerException(HandlerResult result) {
        this(result, 0);
    }

    public ZlinkHandlerException(HandlerResult result, int nativeErrno) {
        super(result.value(), nativeErrno);
        this.result = result;
    }

    public HandlerResult getResult() {
        return result;
    }
}
