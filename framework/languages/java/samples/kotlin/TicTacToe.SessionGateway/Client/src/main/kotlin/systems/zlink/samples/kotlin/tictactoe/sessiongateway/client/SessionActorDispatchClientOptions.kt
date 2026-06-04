package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

data class SessionActorDispatchClientOptions(
    val xActorId: String,
    val oActorId: String,
    val primaryStreamEndpoint: String,
    val reconnectStreamEndpoint: String,
) {
    companion object {
        fun defaults(): SessionActorDispatchClientOptions =
            SessionActorDispatchClientOptions(
                xActorId = SampleNames.XActorId,
                oActorId = SampleNames.OActorId,
                primaryStreamEndpoint = SampleTopology.SessionEndpoint,
                reconnectStreamEndpoint = SampleTopology.SessionEndpoint,
            )
    }
}
