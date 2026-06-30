package systems.zlink.e2e.kotlin.registrationcodec.main.handlers

import org.springframework.beans.factory.ObjectProvider
import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.DiLifecycleReply
import systems.zlink.e2e.kotlin.registrationcodec.DiLifecycleRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrCommand
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoCommand
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualCommand
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualRequest
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.main.infrastructure.DiScopedDependency
import systems.zlink.e2e.kotlin.registrationcodec.main.infrastructure.DiSingletonDependency
import systems.zlink.e2e.kotlin.registrationcodec.main.infrastructure.ScenarioState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.framework.handlers.ZLinkSend

@ZLinkHandlerGroup(Contracts.AUTO_GROUP)
class AutoRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<EchoAutoRequest, EchoReply> {
    override fun handle(request: EchoAutoRequest, context: ZLinkRequestContext): EchoReply {
        state.record("Request", "EchoAuto", request.value)
        return EchoReply("echo:${request.value}", "auto")
    }
}

@ZLinkHandlerGroup(Contracts.AUTO_GROUP)
class AutoSendHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<EchoAutoCommand> {
    override fun handle(message: EchoAutoCommand, context: ZLinkSendContext) {
        state.record("Send", "EchoAuto", message.value)
    }
}

@ZLinkHandlerGroup(Contracts.ATTR_GROUP)
class AttrEchoHandler(
    private val state: ScenarioState,
) {
    @ZLinkRequest(packetName = "EchoAttr")
    fun request(request: EchoAttrRequest, context: ZLinkRequestContext): EchoReply {
        state.record("Request", "EchoAttr", request.value)
        return EchoReply("echo:${request.value}", "attr")
    }

    @ZLinkSend(packetName = "EchoAttr")
    fun send(message: EchoAttrCommand, context: ZLinkSendContext) {
        state.record("Send", "EchoAttr", message.value)
    }
}

class ManualRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<EchoManualRequest, EchoReply> {
    override fun handle(request: EchoManualRequest, context: ZLinkRequestContext): EchoReply {
        state.record("Request", context.packetName().orElse("EchoManual"), request.value)
        return EchoReply("echo:${request.value}", "manual")
    }
}

class ManualSendHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<EchoManualCommand> {
    override fun handle(message: EchoManualCommand, context: ZLinkSendContext) {
        state.record("Send", context.packetName().orElse("EchoManual"), message.value)
    }
}

class DiLifecycleRequestHandler(
    private val scoped: ObjectProvider<DiScopedDependency>,
    private val singleton: DiSingletonDependency,
    private val state: ScenarioState,
) : ZLinkRequestHandler<DiLifecycleRequest, DiLifecycleReply> {
    override fun handle(request: DiLifecycleRequest, context: ZLinkRequestContext): DiLifecycleReply {
        val scopedId = scoped.getObject().use { dependency ->
            state.record(
                "DI",
                context.packetName().orElse("DiLifecycle"),
                "${dependency.id}:${singleton.id}:${request.value}",
            )
            dependency.id
        }
        return DiLifecycleReply(
            "echo:${request.value}",
            scopedId,
            singleton.id,
            state.diDisposeCount(),
        )
    }
}
