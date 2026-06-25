package systems.zlink.e2e.kotlin.registrymessaging

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.channels.ZLinkRouteRequestHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class ProfileRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<ProfileRequest, ProfileReply> {
    override fun handle(
        request: ProfileRequest,
        context: ZLinkRequestContext,
    ): ProfileReply {
        if (request.value.startsWith("slow")) {
            try {
                Thread.sleep(1000)
            } catch (error: InterruptedException) {
                Thread.currentThread().interrupt()
                throw IllegalStateException("interrupted", error)
            }
        }
        state.record("ProfileRequest", request.value)
        return ProfileReply("profile:${request.value}", state.providerRid, state.instanceId)
    }
}

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class ProfileCommandHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<ProfileCommand> {
    override fun handle(
        message: ProfileCommand,
        context: ZLinkSendContext,
    ) {
        state.record("ProfileCommand", message.commandId)
    }
}

class RoutePingHandler(
    private val state: ScenarioState,
) : ZLinkRouteRequestHandler<RoutePing, RoutePong> {
    override fun handle(
        request: RoutePing,
        context: ZLinkRouteRequestContext,
    ): RoutePong {
        state.record("KotlinScenarioRoutePing", request.value)
        return RoutePong("route:${request.value}", state.providerRid, context.routingId().toString())
    }
}
