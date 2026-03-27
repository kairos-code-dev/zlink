/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.StreamSocket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;

public final class StreamCallbackSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        CountDownLatch delivered = new CountDownLatch(1);
        try (Context ctx = new Context();
             StreamSocket server = new StreamSocket(ctx)) {
            server.onReceive(received -> {
                try (received) {
                    System.out.println("stream callback: " + SampleSupport.singleUtf8(received));
                    delivered.countDown();
                }
            });
            server.bind(endpoint);
            try (java.net.Socket client = SampleSupport.connectRawTcp(endpoint)) {
                SampleSupport.sendRawTcp(client,
                    "stream-callback".getBytes(StandardCharsets.UTF_8));
                SampleSupport.await(delivered, "stream callback");
            }
        }
    }
}
