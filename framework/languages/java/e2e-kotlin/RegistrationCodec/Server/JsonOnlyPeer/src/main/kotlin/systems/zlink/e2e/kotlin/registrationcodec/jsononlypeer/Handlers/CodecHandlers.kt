package systems.zlink.e2e.kotlin.registrationcodec.jsononlypeer.handlers

import com.google.protobuf.StringValue
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoMsg
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRes
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoMsg
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoRes
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.jsononlypeer.infrastructure.ScenarioState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler

class JsonRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<JsonEchoReq, JsonEchoRes> {
    override fun handle(request: JsonEchoReq, context: ZLinkRequestContext): JsonEchoRes {
        state.record("Request", "JsonEchoReq", request.value)
        return JsonEchoRes("echo:${request.value}", "json")
    }
}

class JsonSendHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<JsonEchoMsg> {
    override fun handle(message: JsonEchoMsg, context: ZLinkSendContext) {
        state.record("Send", "JsonEchoMsg", message.value)
    }
}

class ProtobufRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<StringValue, StringValue> {
    override fun handle(request: StringValue, context: ZLinkRequestContext): StringValue {
        state.record("Request", "ProtobufEcho", request.value)
        return StringValue.of("echo:${request.value}")
    }
}

class ProtobufSendHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<StringValue> {
    override fun handle(message: StringValue, context: ZLinkSendContext) {
        state.record("Send", "ProtobufEcho", message.value)
    }
}

class MsgpackRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<PackedEchoReq, PackedEchoRes> {
    override fun handle(request: PackedEchoReq, context: ZLinkRequestContext): PackedEchoRes {
        state.record("Request", "PackedEchoReq", request.value)
        return PackedEchoRes("echo:${request.value}")
    }
}

class MsgpackSendHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<PackedEchoMsg> {
    override fun handle(message: PackedEchoMsg, context: ZLinkSendContext) {
        state.record("Send", "PackedEchoMsg", message.value)
    }
}
