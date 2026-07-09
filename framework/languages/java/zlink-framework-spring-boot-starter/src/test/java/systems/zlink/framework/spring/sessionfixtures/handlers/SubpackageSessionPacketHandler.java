package systems.zlink.framework.spring.sessionfixtures.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicInteger;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class SubpackageSessionPacketHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, String> {
    private final AtomicInteger count;
    private final CompletableFuture<Void> handled;

    public SubpackageSessionPacketHandler(
        AtomicInteger count,
        CompletableFuture<Void> handled) {
        this.count = count;
        this.handled = handled;
    }

    @Override
    public String packetName() {
        return "subpackage.session.packet";
    }

    @Override
    public Class<String> messageType() {
        return String.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        String payload) {
        count.incrementAndGet();
        handled.complete(null);
    }
}
