package systems.zlink.samples.tictactoe.sessiongateway.client;

import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public record SessionActorDispatchClientOptions(
    String xActorId,
    String oActorId,
    String primaryStreamEndpoint,
    String reconnectStreamEndpoint) {
    public static SessionActorDispatchClientOptions defaults() {
        return new SessionActorDispatchClientOptions(
            "alice",
            "bob",
            SampleTopology.SessionEndpoint,
            SampleTopology.SessionEndpoint);
    }
}
