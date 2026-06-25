package systems.zlink.e2e.kotlin.registrationcodec.handlers

import com.google.protobuf.StringValue
import systems.zlink.e2e.kotlin.registrationcodec.EchoReply
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoCommand
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRequest
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoCommand
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoReply
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoRequest
import systems.zlink.e2e.kotlin.registrationcodec.ScenarioState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler

class JsonRequestHandler(
    private val state: ScenarioState,
) : ZLinkRequestHandler<JsonEchoRequest, EchoReply> {
    override fun handle(request: JsonEchoRequest, context: ZLinkRequestContext): EchoReply {
        state.record("Request", "JsonEcho", request.value)
        return EchoReply("echo:${request.value}", "json")
    }
}

class JsonSendHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<JsonEchoCommand> {
    override fun handle(message: JsonEchoCommand, context: ZLinkSendContext) {
        state.record("Send", "JsonEcho", message.value)
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
) : ZLinkRequestHandler<PackedEchoRequest, PackedEchoReply> {
    override fun handle(request: PackedEchoRequest, context: ZLinkRequestContext): PackedEchoReply {
        state.record("Request", "MsgpackEcho", request.value)
        return PackedEchoReply("echo:${request.value}")
    }
}

class MsgpackSendHandler(
    private val state: ScenarioState,
) : ZLinkSendHandler<PackedEchoCommand> {
    override fun handle(message: PackedEchoCommand, context: ZLinkSendContext) {
        state.record("Send", "MsgpackEcho", message.value)
    }
}
