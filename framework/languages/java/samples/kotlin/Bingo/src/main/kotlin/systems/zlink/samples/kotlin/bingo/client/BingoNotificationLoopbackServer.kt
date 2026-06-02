package systems.zlink.samples.kotlin.bingo.client

import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.ServerSocket
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
import java.util.concurrent.Executors

class BingoNotificationLoopbackServer(
    port: Int,
) : AutoCloseable {
    private val server = ServerSocket(port)
    private val executor = Executors.newCachedThreadPool { runnable ->
        Thread(runnable, "bingo-kotlin-notification-loopback").apply {
            isDaemon = true
        }
    }

    init {
        executor.execute(::acceptLoop)
    }

    private fun acceptLoop() {
        while (!server.isClosed) {
            try {
                val socket = server.accept()
                executor.execute { serve(socket) }
            } catch (ex: Exception) {
                if (!server.isClosed) {
                    throw IllegalStateException("notification accept failed", ex)
                }
            }
        }
    }

    private fun serve(socket: Socket) {
        try {
            socket.use {
                val input = DataInputStream(it.getInputStream())
                val output = DataOutputStream(it.getOutputStream())
                while (!it.isClosed) {
                    val headerLength = input.readUnsignedShort()
                    val payloadLength = input.readInt()
                    val header = input.readNBytes(headerLength)
                    val payload = input.readNBytes(payloadLength)
                    output.write(encodeSendFrame(decodePacketName(header), payload))
                    output.flush()
                }
            }
        } catch (_: Exception) {
        }
    }

    override fun close() {
        server.close()
        executor.shutdownNow()
    }

    private fun decodePacketName(header: ByteArray): String {
        val buffer = ByteBuffer.wrap(header)
        buffer.get()
        buffer.get()
        val flags = buffer.get().toInt() and 0xff
        if ((flags and 0x01) != 0) {
            buffer.long
        }
        val nameLength = buffer.get().toInt() and 0xff
        val name = ByteArray(nameLength)
        buffer.get(name)
        return String(name, StandardCharsets.UTF_8)
    }

    private fun encodeSendFrame(packetName: String, payload: ByteArray): ByteArray {
        val name = packetName.toByteArray(StandardCharsets.UTF_8)
        val header = ByteBuffer.allocate(3 + 1 + name.size)
            .put(1)
            .put(0)
            .put(0)
            .put(name.size.toByte())
            .put(name)
            .array()
        return ByteBuffer.allocate(6 + header.size + payload.size)
            .putShort(header.size.toShort())
            .putInt(payload.size)
            .put(header)
            .put(payload)
            .array()
    }
}
