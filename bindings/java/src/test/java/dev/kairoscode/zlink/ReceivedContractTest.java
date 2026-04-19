package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotSame;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class ReceivedContractTest {
    @Test
    public void constructorDoesNotAliasCallerArray() {
        try (Message first = Message.copyOfUtf8("first");
             Message second = Message.copyOfUtf8("second");
             Message replacement = Message.copyOfUtf8("replacement")) {
            Message[] parts = {first, second};
            Received received = new Received((RoutingId) null, parts);
            parts[0] = replacement;

            assertSame(first, received.firstPart());
            assertSame(second, received.parts().get(1));
            assertNotSame(replacement, received.firstPart());

            received.close();
        }
    }

    @Test
    public void lazyCursorMaterializesOnlyWhenPartsAreConsumed() {
        try (Message first = Message.copyOfUtf8("first");
             Message second = Message.copyOfUtf8("second");
             Message third = Message.copyOfUtf8("third")) {
            AtomicInteger pulls = new AtomicInteger();
            Message[] trailing = {second, third};
            Received.PartCursor cursor = new Received.PartCursor() {
                private int index;

                @Override
                public Message nextPartOrNull() {
                    int current = index++;
                    if (current >= trailing.length)
                        return null;
                    pulls.incrementAndGet();
                    return trailing[current];
                }

                @Override
                public void close() {
                }
            };

            try (Received received = new Received(null, null, first, cursor,
                0L, false, null, null)) {
                assertSame(first, received.firstPart());
                assertEquals(0, pulls.get());

                var iterator = received.iterator();
                assertTrue(iterator.hasNext());
                assertSame(first, iterator.next());
                assertEquals(0, pulls.get());

                assertTrue(iterator.hasNext());
                assertEquals(1, pulls.get());
                assertSame(second, iterator.next());

                assertTrue(iterator.hasNext());
                assertEquals(2, pulls.get());
                assertSame(third, iterator.next());

                assertEquals(3, received.parts().size());
                assertEquals(2, pulls.get());
            }
        }
    }
}
