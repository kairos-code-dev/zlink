package systems.zlink.samples;

import systems.zlink.Context;
import systems.zlink.MonitorEventType;
import systems.zlink.PairSocket;

public final class MonitorRecvSample {
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

            var serverEvent = serverMonitor.recv();
            var clientEvent = clientMonitor.recv();
            if (serverEvent.event() != MonitorEventType.CONNECTION_READY
                || clientEvent.event() != MonitorEventType.CONNECTION_READY) {
                throw new IllegalStateException("expected connection-ready events");
            }
            System.out.println("[monitor/recv] recv: \"connection-ready\"");
        }
    }
}
