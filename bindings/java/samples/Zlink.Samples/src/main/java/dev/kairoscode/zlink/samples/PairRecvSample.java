/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PairSocket;

public final class PairRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.inprocEndpoint("pair-recv");
        try (Context ctx = new Context();
            PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx)) {
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
