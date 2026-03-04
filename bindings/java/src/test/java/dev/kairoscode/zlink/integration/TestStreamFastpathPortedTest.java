package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertThrows;

public class TestStreamFastpathPortedTest {
    @Test
    public void testStreamFastpathTcpBasic() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.STREAM)) {
            assertThrows(RuntimeException.class,
                () -> server.recv(64, ReceiveFlag.DONTWAIT));

            try (Message msg = new Message()) {
                assertThrows(RuntimeException.class,
                    () -> msg.recv(server, ReceiveFlag.DONTWAIT));
            }
        }
    }
}
