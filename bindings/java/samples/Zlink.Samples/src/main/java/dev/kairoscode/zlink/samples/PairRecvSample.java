/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;

public final class PairRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.inprocEndpoint("pair-recv");
        try (Context ctx = new Context();
            Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            server.bind(endpoint);
            client.connect(endpoint);
            try (Message outbound = Message.copyOfUtf8("pair-copy")) {
                client.send(outbound);
            }
            try (var received = server.recv()) {
                System.out.println("pair recv: " + SampleSupport.singleUtf8(received));
            }
        }
    }
}
