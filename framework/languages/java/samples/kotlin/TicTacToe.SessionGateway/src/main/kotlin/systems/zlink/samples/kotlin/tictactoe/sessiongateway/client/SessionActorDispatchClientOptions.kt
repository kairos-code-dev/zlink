package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

data class SessionActorDispatchClientOptions(
    val xActorId: String,
    val oActorId: String,
    val primaryStreamEndpoint: String,
    val reconnectStreamEndpoint: String,
) {
    companion object {
        fun defaults(): SessionActorDispatchClientOptions =
            SessionActorDispatchClientOptions(
                xActorId = "alice",
                oActorId = "bob",
                primaryStreamEndpoint = SampleTopology.SessionEndpoint,
                reconnectStreamEndpoint = SampleTopology.SessionEndpoint,
            )
    }
}
