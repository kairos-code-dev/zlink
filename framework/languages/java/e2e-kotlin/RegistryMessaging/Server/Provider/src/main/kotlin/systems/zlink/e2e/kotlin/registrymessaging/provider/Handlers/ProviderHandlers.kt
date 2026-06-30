package systems.zlink.e2e.kotlin.registrymessaging.provider.Handlers

import java.security.MessageDigest
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import java.util.HexFormat
import systems.zlink.e2e.kotlin.registrymessaging.provider.Infrastructure.EvidenceStore
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileCommand
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.ScenarioRoutePing
import systems.zlink.e2e.kotlin.registrymessaging.shared.ScenarioRoutePong
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.channels.ZLinkRouteRequestHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent
import systems.zlink.framework.configuration.ZLinkMessageFlowObserver
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome

class ProfileRequestHandler(
    private val evidence: EvidenceStore,
) : ZLinkRequestHandler<ProfileRequest, ProfileReply> {
    override fun handle(request: ProfileRequest, context: ZLinkRequestContext): ProfileReply {
        if (request.value == "slow") {
            Thread.sleep(1000)
        }
        evidence.add("profile-request|rid=${evidence.rid}|value=${request.value}|packet=${context.packetName()}")
        return ProfileReply("profile:${request.value}", evidence.rid)
    }
}

class ProfileCommandHandler(
    private val evidence: EvidenceStore,
) : ZLinkSendHandler<ProfileCommand> {
    override fun handle(message: ProfileCommand, context: ZLinkSendContext) {
        if (message.commandId.startsWith("rm-c9-slow-")) {
            Thread.sleep(1000)
        }
        evidence.add("profile-command|rid=${evidence.rid}|command=${message.commandId}|packet=${context.packetName()}")
    }
}

class PayloadRequestHandler(
    private val evidence: EvidenceStore,
) : ZLinkRequestHandler<PayloadRequest, PayloadReply> {
    override fun handle(request: PayloadRequest, context: ZLinkRequestContext): PayloadReply {
        val digest = MessageDigest.getInstance("SHA-256").digest(request.payload.toByteArray(Charsets.UTF_8))
        val hash = HexFormat.of().formatHex(digest).uppercase()
        evidence.add(
            "payload-request|rid=${evidence.rid}|marker=${request.marker}" +
                "|length=${request.payload.length}|sha256=$hash|packet=${context.packetName()}",
        )
        return PayloadReply(request.marker, request.payload.length, hash)
    }
}

class RoutePingHandler(
    private val evidence: EvidenceStore,
) : ZLinkRouteRequestHandler<ScenarioRoutePing, ScenarioRoutePong> {
    override fun handle(request: ScenarioRoutePing, context: ZLinkRouteRequestContext): ScenarioRoutePong {
        val source = context.routingId().toString()
        evidence.add("route-request|rid=${evidence.rid}|source=$source|value=${request.value}")
        return ScenarioRoutePong("route:${request.value}", evidence.rid, source)
    }
}

class EvidenceDispatchErrorObserver(
    private val evidence: EvidenceStore,
) : ZLinkMessageFlowObserver {
    override fun onMessageFlow(flow: ZLinkMessageFlowEvent): CompletionStage<Void> {
        if (flow.outcome() != ZLinkMessageFlowOutcome.ERROR) {
            return CompletableFuture.completedFuture(null)
        }
        evidence.add(
            "dispatch-error" +
                "|surface=${flow.surface()}" +
                "|kind=${flow.messageKind()}" +
                "|reason=${flow.errorReason()}" +
                "|action=${flow.errorAction()}" +
                "|packet=${flow.packetName() ?: "<null>"}" +
                "|channel=${flow.channelName() ?: "<null>"}",
        )
        return CompletableFuture.completedFuture(null)
    }
}
