package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class NativeContractTest {
    private static final int ZLINK_ROUTER_OPT_MANDATORY = 0x3101;
    private static final int ZLINK_PUB_OPT_VERBOSE = 0x3301;
    private static final int ZLINK_PUB_OPT_TOPICS_COUNT = 0x3307;
    private static final int ZLINK_SUB_OPT_TOPICS_COUNT = 0x3400;
    private static final int ZLINK_STREAM_OPT_NOTIFY = 0x3501;

    @Test
    public void testRawMultipartSendRecvSinglePart() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket left = new PairSocket(ctx);
             PairSocket right = new PairSocket(ctx);
             Arena arena = Arena.ofConfined()) {
            String endpoint = TestSupport.inprocEndpoint("native-send");
            left.bind(endpoint);
            right.connect(endpoint);

            byte[] payload = "native".getBytes(StandardCharsets.UTF_8);
            MemorySegment msg = arena.allocate(
                NativeLayouts.MSG_LAYOUT.byteSize(),
                NativeLayouts.MSG_LAYOUT.byteAlignment());
            assertEquals(0, NativeMsg.msgInitSize(msg, payload.length));
            MemorySegment.copy(MemorySegment.ofArray(payload), 0,
                NativeMsg.msgData(msg).reinterpret(payload.length), 0,
                payload.length);

            assertEquals(0,
                Native.sendMultipart(right.handle(), msg, 1,
                    SendFlag.NONE.getValue()));

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
             StreamSocket stream = new StreamSocket(ctx);
             Arena arena = Arena.ofConfined()) {
            MemorySegment intValue = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment sizeValue = arena.allocate(ValueLayout.JAVA_LONG);

            intValue.set(ValueLayout.JAVA_INT, 0, 1);
            assertEquals(0, Native.setRouterOption(router.handle(),
                ZLINK_ROUTER_OPT_MANDATORY, intValue,
                ValueLayout.JAVA_INT.byteSize()));
            sizeValue.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            intValue.set(ValueLayout.JAVA_INT, 0, 0);
            assertEquals(0, Native.getRouterOption(router.handle(),
                ZLINK_ROUTER_OPT_MANDATORY, intValue, sizeValue));
            assertEquals(1, intValue.get(ValueLayout.JAVA_INT, 0));

            intValue.set(ValueLayout.JAVA_INT, 0, 1);
            assertEquals(0, Native.setPubOption(xpub.handle(),
                ZLINK_PUB_OPT_VERBOSE, intValue,
                ValueLayout.JAVA_INT.byteSize()));
            sizeValue.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            intValue.set(ValueLayout.JAVA_INT, 0, 0);
            assertEquals(0, Native.getPubOption(xpub.handle(),
                ZLINK_PUB_OPT_TOPICS_COUNT, intValue, sizeValue));
            assertEquals(0, intValue.get(ValueLayout.JAVA_INT, 0));

            assertEquals(0, Native.setSubscription(sub.handle(),
                arena.allocateFrom("native-contract")));
            intValue.set(ValueLayout.JAVA_INT, 0, 1);
            assertEquals(-1, Native.setSubOption(sub.handle(),
                ZLINK_SUB_OPT_TOPICS_COUNT, intValue,
                ValueLayout.JAVA_INT.byteSize()));
            sizeValue.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            intValue.set(ValueLayout.JAVA_INT, 0, -1);
            assertEquals(0, Native.getSubOption(sub.handle(),
                ZLINK_SUB_OPT_TOPICS_COUNT, intValue, sizeValue));
            assertEquals(1, intValue.get(ValueLayout.JAVA_INT, 0));

            intValue.set(ValueLayout.JAVA_INT, 0, 1);
            assertEquals(0, Native.setStreamOption(stream.handle(),
                ZLINK_STREAM_OPT_NOTIFY, intValue,
                ValueLayout.JAVA_INT.byteSize()));
            sizeValue.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            intValue.set(ValueLayout.JAVA_INT, 0, 0);
            assertEquals(0, Native.getStreamOption(stream.handle(),
                ZLINK_STREAM_OPT_NOTIFY, intValue, sizeValue));
            assertEquals(1, intValue.get(ValueLayout.JAVA_INT, 0));
        }
    }
}
