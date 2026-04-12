package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;

public final class MonitorRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx);
             var serverMonitor = server.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);
             var clientMonitor = client.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY)) {
            server.bind(endpoint);
            client.connect(endpoint);

            var serverEvent = serverMonitor.recv();
            var clientEvent = clientMonitor.recv();
            if (serverEvent.event() != MonitorEventType.CONNECTION_READY.getValue()
                || clientEvent.event() != MonitorEventType.CONNECTION_READY.getValue()) {
                throw new IllegalStateException("expected connection-ready events");
            }
            try {
                serverMonitor.recv(RecvFlags.DONT_WAIT);
                throw new IllegalStateException(
                    "monitor recv(DONT_WAIT) unexpectedly returned an event");
            } catch (RecvException ex) {
                if (ex.getResult() != RecvResult.NO_DATA) {
                    throw ex;
                }
            }
            try {
                clientMonitor.recv(RecvFlags.DONT_WAIT);
                throw new IllegalStateException(
                    "monitor recv(DONT_WAIT) unexpectedly returned an event");
            } catch (RecvException ex) {
                if (ex.getResult() != RecvResult.NO_DATA) {
                    throw ex;
                }
            }
            System.out.println("[monitor/recv] recv: \"connection-ready\" \u2192 recv(DONT_WAIT): empty");
        }
    }
}
