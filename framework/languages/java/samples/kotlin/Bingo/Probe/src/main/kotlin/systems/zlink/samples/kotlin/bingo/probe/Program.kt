package systems.zlink.samples.kotlin.bingo.probe

import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.time.Duration
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.bingo.probe.configuration.SampleTopology

fun main(args: Array<String>) = runBlocking {
    val timeout = Duration.ofSeconds((readOption(args, "--timeout-seconds") ?: "10").toLong())
    val deadline = System.nanoTime() + timeout.toNanos()
    val endpoints = listOf(
        SampleTopology.RegistryPubEndpoint,
        SampleTopology.RegistryRouterEndpoint,
        SampleTopology.ApiChannelEndpoint,
        SampleTopology.PlayChannelEndpoint,
        SampleTopology.SessionSpotEndpoint,
        SampleTopology.SessionRouterEndpoint,
        SampleTopology.PlaySpotEndpoint,
        SampleTopology.PlaySpotRouterEndpoint,
        SampleTopology.StreamEndpoint,
    )
    while (System.nanoTime() < deadline) {
        if (endpoints.all(::canConnect)) {
            println("topology=ready")
            return@runBlocking
        }
        delay(100)
    }
    error("Timed out waiting for Bingo sample topology readiness.")
}

private fun canConnect(endpoint: String): Boolean {
    val uri = URI.create(endpoint)
    return try {
        Socket().use { socket ->
            socket.connect(InetSocketAddress(uri.host, uri.port), 100)
        }
        true
    } catch (_: Exception) {
        false
    }
}

private fun readOption(args: Array<String>, name: String): String? {
    val index = args.indexOf(name)
    return if (index >= 0 && index + 1 < args.size) args[index + 1] else null
}
