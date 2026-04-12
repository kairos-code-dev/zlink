/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public final class HandlerException extends ZlinkException {
    private final HandlerResult result;

    public HandlerException(HandlerResult result) {
        this(result, 0);
    }

    public HandlerException(HandlerResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public HandlerResult getResult() {
        return result;
    }
}
