package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers.OpenConversationActorHandler
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers.SetAgentAvailableHandler

class SupportEntrySpot(
    private val context: ZLinkEntrySpotContext,
) : ZLinkEntrySpot {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {
        context.handlers().addHandler(OpenConversationActorHandler::class.java)
        context.handlers().addHandler(SetAgentAvailableHandler::class.java)
    }

    override fun onCreateActor(actor: ZLinkActor, cancellationToken: CancellationToken) = Unit

    override fun onJoinActor(actor: ZLinkActor, cancellationToken: CancellationToken) = Unit

    override fun onLeaveActor(actor: ZLinkActor, cancellationToken: CancellationToken) = Unit

    override fun onDisconnectActor(actor: ZLinkActor, cancellationToken: CancellationToken) {
        if (actor is SupportUserActor) {
            actor.markDisconnected()
        }
    }
}
