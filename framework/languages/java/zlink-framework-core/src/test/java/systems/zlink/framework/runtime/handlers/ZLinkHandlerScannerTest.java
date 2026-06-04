package systems.zlink.framework.runtime.handlers;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkRequest;

final class ZLinkHandlerScannerTest {
    @Test
    void scansUngroupedInterfaceHandlersLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler handler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == UngroupedInterfaceHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerSurface.CHANNEL, handler.surface());
        assertEquals(ZLinkScannedHandlerKind.REQUEST, handler.kind());
        assertEquals(String.class, handler.messageType());
        assertEquals(String.class, handler.replyType());
        assertTrue(handler.groups().isEmpty());
        assertTrue(catalog.matching(
            Set.of("missing"),
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.REQUEST).stream()
            .noneMatch(candidate -> candidate.handlerType() == UngroupedInterfaceHandler.class));
    }

    @Test
    void scansUngroupedAttributedHandlersLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler handler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == UngroupedAttributedHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerSurface.CHANNEL, handler.surface());
        assertEquals(ZLinkScannedHandlerKind.REQUEST, handler.kind());
        assertEquals("Echo", handler.packetName());
        assertTrue(handler.groups().isEmpty());
    }

    public static final class UngroupedInterfaceHandler
        implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(
            String request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class UngroupedAttributedHandler {
        @ZLinkRequest(packetName = "Echo")
        public CompletionStage<String> handle(String request) {
            return CompletableFuture.completedFuture(request);
        }
    }
}
