/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.runtime.nativeapi.ContractAccess;

/**
 * An opaque, framework-owned handle for a prepared actor transfer fence.
 *
 * <p>The token is passed unchanged to commit, activate, or abort. Applications
 * must not inspect or persist its bytes.</p>
 */
public final class ActorTransferToken {
    private final byte[] opaque;

    private ActorTransferToken(byte[] opaque) {
        this.opaque = opaque;
    }

    static {
        ContractAccess.register(new ContractAccess.ActorTransferTokenAccess() {
            @Override
            public ActorTransferToken create(byte[] opaque) {
                return new ActorTransferToken(opaque);
            }

            @Override
            public byte[] opaque(ActorTransferToken token) {
                return token.opaque;
            }
        });
    }
}
