package systems.zlink.e2e.kotlin.registrationcodec.main.endpoints

import com.fasterxml.jackson.databind.ObjectMapper
import com.google.protobuf.StringValue
import com.sun.net.httpserver.HttpExchange
import systems.zlink.e2e.kotlin.registrationcodec.CodecRoundtripRes
import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.DiLifecycleReq
import systems.zlink.e2e.kotlin.registrationcodec.DiLifecycleRes
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrMsg
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrReq
import systems.zlink.e2e.kotlin.registrationcodec.EchoAttrRes
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoMsg
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoReq
import systems.zlink.e2e.kotlin.registrationcodec.EchoAutoRes
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualMsg
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualReq
import systems.zlink.e2e.kotlin.registrationcodec.EchoManualRes
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoMsg
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.JsonEchoRes
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoMsg
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoReq
import systems.zlink.e2e.kotlin.registrationcodec.PackedEchoRes
import systems.zlink.framework.channels.ZLinkClient

class RegistrationScenarioEndpoints(
    private val client: ZLinkClient,
    private val json: ObjectMapper,
) {
    fun map(httpServer: com.sun.net.httpserver.HttpServer) {
        httpServer.createContext("/registration/auto") { exchange ->
            val reply = client.requestToChannel(Contracts.CHANNEL, EchoAutoReq("auto-request"))
                .packetName("EchoAutoReq")
                .await(EchoAutoRes::class.java)
            client.sendToChannel(Contracts.CHANNEL, EchoAutoMsg("auto-send"))
                .packetName("EchoAutoMsg")
                .await()
            exchange.writeJson(reply)
        }
        httpServer.createContext("/registration/attribute") { exchange ->
            val reply = client.requestToChannel(Contracts.CHANNEL, EchoAttrReq("attr-request"))
                .packetName("EchoAttrReq")
                .await(EchoAttrRes::class.java)
            client.sendToChannel(Contracts.CHANNEL, EchoAttrMsg("attr-send"))
                .packetName("EchoAttrMsg")
                .await()
            exchange.writeJson(reply)
        }
        httpServer.createContext("/registration/manual") { exchange ->
            val reply = client.requestToChannel(Contracts.CHANNEL, EchoManualReq("manual-request"))
                .packetName("EchoManualReq")
                .await(EchoManualRes::class.java)
            client.sendToChannel(Contracts.CHANNEL, EchoManualMsg("manual-send"))
                .packetName("EchoManualMsg")
                .await()
            exchange.writeJson(reply)
        }
        httpServer.createContext("/registration/di-lifecycle") { exchange ->
            val replies = (0 until 3).map { index ->
                client.requestToChannel(Contracts.CHANNEL, DiLifecycleReq("di-$index"))
                    .packetName("DiLifecycleReq")
                    .await(DiLifecycleRes::class.java)
            }
            exchange.writeJson(replies)
        }
        httpServer.createContext("/registration/filter-order") { exchange ->
            val reply = client.requestToChannel(Contracts.CHANNEL, EchoManualReq("filter-order-request"))
                .packetName("EchoManualReq")
                .await(EchoManualRes::class.java)
            exchange.writeJson(reply)
        }
        httpServer.createContext("/codec/json") { exchange ->
            val reply = client.requestToChannel(Contracts.CHANNEL, JsonEchoReq("json-request"))
                .packetName("JsonEchoReq")
                .await(JsonEchoRes::class.java)
            client.sendToChannel(Contracts.CHANNEL, JsonEchoMsg("json-send"))
                .packetName("JsonEchoMsg")
                .await()
            exchange.writeJson(reply)
        }
        httpServer.createContext("/codec/protobuf") { exchange ->
            val reply = client.requestToChannel(Contracts.CHANNEL, StringValue.of("protobuf-request"))
                .packetName("ProtobufEcho")
                .await(StringValue::class.java)
            client.sendToChannel(Contracts.CHANNEL, StringValue.of("protobuf-send"))
                .packetName("ProtobufEcho")
                .await()
            exchange.writeJson(mapOf("value" to reply.value))
        }
        httpServer.createContext("/codec/messagepack") { exchange ->
            val reply = client.requestToChannel(Contracts.CHANNEL, PackedEchoReq("msgpack-request"))
                .packetName("PackedEchoReq")
                .await(PackedEchoRes::class.java)
            client.sendToChannel(Contracts.CHANNEL, PackedEchoMsg("msgpack-send"))
                .packetName("PackedEchoMsg")
                .await()
            exchange.writeJson(reply)
        }
        httpServer.createContext("/codec/roundtrip") { exchange ->
            val jsonReply = client.requestToChannel(Contracts.CHANNEL, JsonEchoReq("json-request"))
                .packetName("JsonEchoReq")
                .await(JsonEchoRes::class.java)
            val protobufReply = client.requestToChannel(Contracts.CHANNEL, StringValue.of("protobuf-request"))
                .packetName("ProtobufEcho")
                .await(StringValue::class.java)
            val packedReply = client.requestToChannel(Contracts.CHANNEL, PackedEchoReq("msgpack-request"))
                .packetName("PackedEchoReq")
                .await(PackedEchoRes::class.java)
            exchange.writeJson(CodecRoundtripRes(jsonReply.value, protobufReply.value, packedReply.value))
        }
    }

    private fun HttpExchange.writeJson(value: Any) {
        val body = json.writeValueAsBytes(value)
        responseHeaders.add("Content-Type", "application/json")
        sendResponseHeaders(200, body.size.toLong())
        responseBody.use { it.write(body) }
    }
}
