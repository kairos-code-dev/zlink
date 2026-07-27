package systems.zlink.framework.runtime.internal.binding.spot;

/**
 * Creates opaque Framework transfer tokens for fake backend implementations.
 */
public final class ActorTransferTokenFixture {
    private ActorTransferTokenFixture() {
    }

    public static ActorTransferToken create(byte[] opaque) {
        return new ActorTransferToken(opaque);
    }
}
