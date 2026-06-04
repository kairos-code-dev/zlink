package systems.zlink.samples.tictactoe.sessiongateway.client;

import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public record SessionActorDispatchClientOptions(
    String xActorId,
    String oActorId,
    String primaryStreamEndpoint,
    String reconnectStreamEndpoint) {
    public static SessionActorDispatchClientOptions defaults() {
        return new SessionActorDispatchClientOptions(
            SampleNames.XActorId,
            SampleNames.OActorId,
            SampleTopology.SessionEndpoint,
            SampleTopology.SessionEndpoint);
    }
}
