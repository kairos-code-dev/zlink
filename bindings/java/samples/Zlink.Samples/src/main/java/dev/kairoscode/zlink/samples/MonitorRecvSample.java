package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;

public final class MonitorRecvSample {
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

            var serverEvent = serverMonitor.recv();
            var clientEvent = clientMonitor.recv();
            if (serverEvent.event() != MonitorEventType.CONNECTION_READY.getValue()
                || clientEvent.event() != MonitorEventType.CONNECTION_READY.getValue()) {
                throw new IllegalStateException("expected connection-ready events");
            }
            if (serverMonitor.tryRecv().isPresent() || clientMonitor.tryRecv().isPresent()) {
                throw new IllegalStateException("monitor tryRecv unexpectedly returned an event");
            }
            System.out.println("[monitor/recv] recv: \"connection-ready\" \u2192 tryRecv: empty");
        }
    }
}
