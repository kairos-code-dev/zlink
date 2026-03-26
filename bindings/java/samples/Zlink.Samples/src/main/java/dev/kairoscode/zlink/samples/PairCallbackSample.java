/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import java.util.concurrent.CountDownLatch;

public final class PairCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.inprocEndpoint("pair-callback");
        CountDownLatch delivered = new CountDownLatch(1);
        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            server.onReceive(received -> {
                try (received) {
                    System.out.println("pair callback: " + SampleSupport.singleUtf8(received));
                    delivered.countDown();
                }
            });
            server.bind(endpoint);
            client.connect(endpoint);
            try (Message outbound = SampleSupport.wrapUtf8("pair-wrap")) {
                client.send(outbound);
            }
            SampleSupport.await(delivered, "pair callback");
        }
    }
}
