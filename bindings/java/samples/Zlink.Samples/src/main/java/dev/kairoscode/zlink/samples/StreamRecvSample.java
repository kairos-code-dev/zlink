/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.StreamSocket;
import java.nio.charset.StandardCharsets;

public final class StreamRecvSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        try (Context ctx = new Context();
             StreamSocket server = new StreamSocket(ctx)) {
            server.bind(endpoint);
            try (java.net.Socket client = SampleSupport.connectRawTcp(endpoint)) {
                SampleSupport.sendRawTcp(client,
                    "stream-ping".getBytes(StandardCharsets.UTF_8));
                RoutingId rid;
                try (var received = server.recv()) {
                    rid = received.routingId();
                    System.out.println("stream recv: " + SampleSupport.singleUtf8(received));
                }
                try (Message replyPart = Message.copyOfUtf8("stream-pong")) {
                    server.send(rid, replyPart);
                }
                byte[] reply = SampleSupport.recvExactRawTcp(client,
                    "stream-pong".length());
                System.out.println("stream reply: "
                    + new String(reply, StandardCharsets.UTF_8));
            }
        }
    }
}
