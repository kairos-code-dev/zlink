package systems.zlink.samples;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.PairSocket;

public final class PairRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx);
             var serverMonitor = server.monitorOpen(
                 systems.zlink.MonitorEventType.CONNECTION_READY);
             var clientMonitor = client.monitorOpen(
                 systems.zlink.MonitorEventType.CONNECTION_READY)) {
            server.bind(endpoint);
            client.connect(endpoint);
            SampleSupport.waitConnected(serverMonitor, clientMonitor);

            try (Message outbound = Message.copyOfUtf8(SampleSupport.PAIR_PAYLOAD)) {
                client.send(outbound);
            }

            try (systems.zlink.Received received = new systems.zlink.Received()) {
                server.recv(received, systems.zlink.RecvFlags.NONE);
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
