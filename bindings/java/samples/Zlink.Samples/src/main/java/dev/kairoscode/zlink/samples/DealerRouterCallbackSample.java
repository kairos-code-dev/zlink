/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import java.util.concurrent.CountDownLatch;

public final class DealerRouterCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.inprocEndpoint("dealer-router-callback");
        CountDownLatch delivered = new CountDownLatch(1);
        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer = new Socket(ctx, SocketType.DEALER)) {
            dealer.setRoutingId(RoutingId.copyOf("dealer-b".getBytes()));
            router.onReceive(received -> {
                try (received) {
                    System.out.println("dealer/router callback: "
                      + SampleSupport.singleUtf8(received));
                    delivered.countDown();
                }
            });
            router.bind(endpoint);
            dealer.connect(endpoint);
            try (Message outbound = Message.copyOfUtf8("callback")) {
                dealer.send(outbound);
            }
            SampleSupport.await(delivered, "dealer/router callback");
        }
    }
}
