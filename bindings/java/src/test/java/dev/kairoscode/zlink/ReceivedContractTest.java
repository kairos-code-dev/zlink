package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertNotSame;
import static org.junit.jupiter.api.Assertions.assertSame;

public class ReceivedContractTest {
    @Test
    public void constructorDoesNotAliasCallerArray() {
        try (Message first = Message.copyOfUtf8("first");
             Message second = Message.copyOfUtf8("second");
             Message replacement = Message.copyOfUtf8("replacement")) {
            Message[] parts = {first, second};
            Received received = new Received(null, parts);
            parts[0] = replacement;

            assertSame(first, received.firstPart());
            assertSame(second, received.parts().get(1));
            assertNotSame(replacement, received.firstPart());

            received.close();
        }
    }
}
