package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpExchange
import java.nio.charset.StandardCharsets
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import java.util.concurrent.CountDownLatch
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

object CreateGameHttpHandler {
    fun handle(exchange: HttpExchange, client: ZLinkClient, json: ObjectMapper) {
        if (exchange.requestMethod != "POST") {
            write(exchange, 405, "")
            return
        }

        try {
            val request = json.readValue(exchange.requestBody, CreateGameHttpReq::class.java)
            val game = await(
                client.requestToChannel(
                    SampleNames.PlayChannel,
                    CreateGameReq(request.gameName?.takeIf { it.isNotBlank() } ?: "tictactoe-game"),
                )
                    .packetName("CreateGameReq")
                    .submitAsync(CreateGameRes::class.java),
            )
            write(
                exchange,
                200,
                json.writeValueAsString(CreateGameHttpRes(game.gameId, game.playEndpoint, game.gameName)),
            )
        } catch (ex: RuntimeException) {
            write(exchange, 500, ex.message.orEmpty())
        }
    }

    private fun <T> await(stage: CompletionStage<T>): T {
        val done = CompletableFuture<T>()
        val latch = CountDownLatch(1)
        stage.whenComplete { value, error ->
            if (error != null) {
                done.completeExceptionally(error)
            } else {
                done.complete(value)
            }
            latch.countDown()
        }
        try {
            latch.await()
            return done.get()
        } catch (ex: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("Interrupted while waiting for API reply.", ex)
        } catch (ex: java.util.concurrent.ExecutionException) {
            throw IllegalStateException("Create game request failed.", ex.cause)
        }
    }

    private fun write(exchange: HttpExchange, statusCode: Int, body: String) {
        val bytes = body.toByteArray(StandardCharsets.UTF_8)
        exchange.responseHeaders.set("Content-Type", "application/json")
        exchange.sendResponseHeaders(statusCode, bytes.size.toLong())
        exchange.responseBody.write(bytes)
        exchange.close()
    }
}
