package systems.zlink.samples.kotlin.deliverydispatch.probe

import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.time.Duration
import java.util.concurrent.locks.LockSupport
import systems.zlink.samples.kotlin.deliverydispatch.probe.configuration.SampleTopology

fun main(args: Array<String>) {
    val timeout = Duration.ofSeconds(readOption(args, "--timeout-seconds", "10").toLong())
    val deadline = System.nanoTime() + timeout.toNanos()
    val endpoints = listOf(
        SampleTopology.RegistryPubEndpoint,
        SampleTopology.RegistryRouterEndpoint,
        SampleTopology.TrackingChannelEndpoint,
        SampleTopology.StatusFanoutEndpoint,
        SampleTopology.TrackingSpotRouterEndpoint,
        SampleTopology.SessionStreamEndpoint,
    )
    while (System.nanoTime() < deadline) {
        if (endpoints.all { canConnect(it) }) {
            println("topology=ready")
            return
        }
        LockSupport.parkNanos(Duration.ofMillis(100).toNanos())
    }
    throw IllegalStateException("Timed out waiting for DeliveryDispatch sample topology readiness.")
}

private fun canConnect(endpoint: String): Boolean {
    val uri = URI.create(endpoint)
    return try {
        Socket().use { socket ->
            socket.connect(InetSocketAddress(uri.host, uri.port), 100)
            true
        }
    } catch (ex: Exception) {
        false
    }
}

private fun readOption(args: Array<String>, name: String, defaultValue: String): String {
    var index = 0
    while (index + 1 < args.size) {
        if (args[index] == name) {
            return args[index + 1]
        }
        index++
    }
    return defaultValue
}
