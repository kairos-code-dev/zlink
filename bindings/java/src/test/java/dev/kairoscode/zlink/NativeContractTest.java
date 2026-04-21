package dev.kairoscode.zlink;

import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class NativeContractTest {
    @Test
    public void testRawMultipartSendRecvSinglePart() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket left = new PairSocket(ctx);
             PairSocket right = new PairSocket(ctx)) {
            String endpoint = TestSupport.inprocEndpoint("native-send");
            left.bind(endpoint);
            right.connect(endpoint);

            byte[] payload = "native".getBytes(StandardCharsets.UTF_8);
            try (Message outbound = Message.copyOf(payload)) {
                assertTrue(right.send(outbound, SendFlags.NONE));
            }

            try (Received inbound = left.recv()) {
                assertArrayEquals(payload,
                    inbound.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void testDedicatedOptionFamilyDowncalls() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             XPubSocket xpub = new XPubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             StreamSocket stream = new StreamSocket(ctx)) {
            router.options().mandatory(true);
            assertTrue(router.options().mandatory());

            xpub.options().verbose(true);
            assertEquals(0, xpub.options().topicsCount());

            sub.setSubscription("native-contract");
            assertEquals(1, sub.options().topicsCount());

            stream.options().notify(true);
            assertTrue(stream.options().notifyEnabled());
        }
    }
}
