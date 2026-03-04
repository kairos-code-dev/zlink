package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestXpubTopicPortedTest {
    @Test
    public void testXpubSubscribeUnsubscribeLongTopics() {
        TestSupport.assumeNative();

        String shortTopic = repeated('A', 245);
        String longTopic = repeated('A', 246);

        try (Context ctx = new Context();
             Socket xpub = new Socket(ctx, SocketType.XPUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpoint = TestSupport.tcpEndpoint();
            xpub.bind(endpoint);
            sub.connect(endpoint);

            assertSubscribeUnsubscribe(xpub, sub, shortTopic);
            assertSubscribeUnsubscribe(xpub, sub, longTopic);
        }
    }

    private static void assertSubscribeUnsubscribe(Socket xpub, Socket sub,
                                                   String topic) {
        byte[] topicBytes = topic.getBytes(StandardCharsets.UTF_8);

        sub.setSockOpt(SocketOption.SUBSCRIBE, topicBytes);
        byte[] subscribe = TestSupport.recvWithTimeout(xpub,
            topicBytes.length + 8, TestSupport.DEFAULT_TIMEOUT_MS);
        assertEquals(topicBytes.length + 1, subscribe.length);
        assertEquals(1, subscribe[0]);
        assertArrayEquals(topicBytes,
            Arrays.copyOfRange(subscribe, 1, subscribe.length));

        sub.setSockOpt(SocketOption.UNSUBSCRIBE, topicBytes);
        byte[] unsubscribe = TestSupport.recvWithTimeout(xpub,
            topicBytes.length + 8, TestSupport.DEFAULT_TIMEOUT_MS);
        assertEquals(topicBytes.length + 1, unsubscribe.length);
        assertEquals(0, unsubscribe[0]);
        assertArrayEquals(topicBytes,
            Arrays.copyOfRange(unsubscribe, 1, unsubscribe.length));
    }

    private static String repeated(char ch, int count) {
        char[] chars = new char[count];
        Arrays.fill(chars, ch);
        return new String(chars);
    }
}
