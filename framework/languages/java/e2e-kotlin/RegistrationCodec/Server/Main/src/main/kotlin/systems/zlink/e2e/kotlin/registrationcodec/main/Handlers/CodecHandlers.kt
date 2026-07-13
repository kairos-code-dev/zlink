package systems.zlink.e2e.kotlin.registrationcodec.main.handlers

import com.google.protobuf.StringValue
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoMsg
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRes
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoMsg
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoRes
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.main.infrastructure.ScenarioState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingSendHandler

class JsonRequestHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingRequestHandler<JsonEchoReq, JsonEchoRes> {
    override suspend fun handle(request: JsonEchoReq, context: ZLinkRequestContext): JsonEchoRes {
        state.record("Request", "JsonEchoReq", request.value)
        return JsonEchoRes("echo:${request.value}", "json")
    }
}

class JsonSendHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingSendHandler<JsonEchoMsg> {
    override suspend fun handle(message: JsonEchoMsg, context: ZLinkSendContext) {
        state.record("Send", "JsonEchoMsg", message.value)
    }
}

class ProtobufRequestHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingRequestHandler<StringValue, StringValue> {
    override suspend fun handle(request: StringValue, context: ZLinkRequestContext): StringValue {
        state.record("Request", "ProtobufEcho", request.value)
        return StringValue.of("echo:${request.value}")
    }
}

class ProtobufSendHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingSendHandler<StringValue> {
    override suspend fun handle(message: StringValue, context: ZLinkSendContext) {
        state.record("Send", "ProtobufEcho", message.value)
    }
}

class MsgpackRequestHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingRequestHandler<PackedEchoReq, PackedEchoRes> {
    override suspend fun handle(request: PackedEchoReq, context: ZLinkRequestContext): PackedEchoRes {
        state.record("Request", "PackedEchoReq", request.value)
        return PackedEchoRes("echo:${request.value}")
    }
}

class MsgpackSendHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingSendHandler<PackedEchoMsg> {
    override suspend fun handle(message: PackedEchoMsg, context: ZLinkSendContext) {
        state.record("Send", "PackedEchoMsg", message.value)
    }
}
