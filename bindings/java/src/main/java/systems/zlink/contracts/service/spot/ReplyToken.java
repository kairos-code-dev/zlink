/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.runtime.nativeapi.ContractAccess;

/**
 * An opaque handle that authorizes a single reply to a received request or
 * actor-join. Obtained from a {@link ReceiveRecord}; consumed by
 * {@link Dispatch#reply} or {@link Dispatch#actorJoinReply}.
 */
public final class ReplyToken {
    private final byte[] opaque;

    ReplyToken(byte[] opaque) {
        this.opaque = opaque;
    }

    byte[] opaque() {
        return opaque;
    }

    static {
        ContractAccess.register(new ContractAccess.ReplyTokenAccess() {
            @Override
            public ReplyToken create(byte[] opaque) {
                return new ReplyToken(opaque);
            }

            @Override
            public byte[] opaque(ReplyToken token) {
                return token.opaque;
            }
        });
    }
}
