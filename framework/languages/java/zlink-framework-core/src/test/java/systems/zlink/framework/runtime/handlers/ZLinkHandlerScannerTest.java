package systems.zlink.framework.runtime.handlers;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;

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

    @Test
    void scansAttributedHandlersWithContextParameterLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler requestHandler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == ContextAttributedRequestHandler.class)
            .findFirst()
            .orElseThrow();
        ZLinkScannedHandler sendHandler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == ContextAttributedSendHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerKind.REQUEST, requestHandler.kind());
        assertEquals("ContextRequest", requestHandler.packetName());
        assertEquals(ZLinkScannedHandlerKind.SEND, sendHandler.kind());
        assertEquals("ContextSend", sendHandler.packetName());
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

    public static final class ContextAttributedRequestHandler {
        @ZLinkRequest(packetName = "ContextRequest")
        public CompletionStage<String> handle(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(context.packetName().orElse(request));
        }
    }

    public static final class ContextAttributedSendHandler {
        @ZLinkSend(packetName = "ContextSend")
        public CompletionStage<Void> handle(String request, ZLinkHandlerContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
