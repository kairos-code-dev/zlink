package systems.zlink.samples.kotlin.tictactoe.server.configuration

import java.net.ServerSocket

object SamplePorts {
    fun reserve(): Int =
        ServerSocket(0).use { socket ->
            socket.reuseAddress = true
            socket.localPort
        }
}
