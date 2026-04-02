package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PairSocket;

public final class PairRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx);
             var serverMonitor = server.monitorOpen(SampleSupport.CONNECTION_READY_EVENT);
             var clientMonitor = client.monitorOpen(SampleSupport.CONNECTION_READY_EVENT)) {
            server.bind(endpoint);
            client.connect(endpoint);
            SampleSupport.waitConnected(serverMonitor, clientMonitor);

            try (Message outbound = Message.copyOfUtf8(SampleSupport.PAIR_PAYLOAD)) {
                client.send(outbound);
            }

            try (var received = server.recv()) {
                String value = SampleSupport.singleUtf8(received);
                if (!SampleSupport.PAIR_PAYLOAD.equals(value)) {
                    throw new IllegalStateException("unexpected payload: " + value);
                }
                System.out.println("[pair/recv] send: \"" + SampleSupport.PAIR_PAYLOAD
                    + "\" \u2192 recv: \"" + value + "\"");
            }
        }
    }
}
