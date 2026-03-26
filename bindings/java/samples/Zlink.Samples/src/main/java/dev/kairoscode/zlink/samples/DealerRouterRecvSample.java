/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;

public final class DealerRouterRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.inprocEndpoint("dealer-router-recv");
        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer = new Socket(ctx, SocketType.DEALER)) {
            dealer.setRoutingId(RoutingId.copyOf("dealer-a".getBytes()));
            router.bind(endpoint);
            dealer.connect(endpoint);
            try (Message outbound = Message.copyOfUtf8("ping")) {
                dealer.send(outbound);
            }
            RoutingId rid;
            try (var received = router.recv()) {
                rid = received.routingId();
                System.out.println("dealer->router: " + SampleSupport.singleUtf8(received));
            }
            try (Message replyPart = Message.copyOfUtf8("pong")) {
                router.send(rid, replyPart);
            }
            try (var reply = dealer.recv()) {
                System.out.println("router->dealer: " + SampleSupport.singleUtf8(reply));
            }
        }
    }
}
