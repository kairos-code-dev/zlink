package systems.zlink.samples.kotlin.streamingclient

import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.ServerSocket
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
import java.util.concurrent.Executors

class StreamingClientLoopbackServer(
    port: Int,
) : AutoCloseable {
    private val server = ServerSocket(port)
    private val executor = Executors.newCachedThreadPool { runnable ->
        Thread(runnable, "streaming-client-kotlin-loopback").apply { isDaemon = true }
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
                    throw IllegalStateException("loopback accept failed", ex)
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
                    val header = decodeHeader(input.readNBytes(headerLength))
                    val payload = input.readNBytes(payloadLength)
                    writeFrame(output, encodeHeader(1, null, header.name, header.metadata), payload)
                    if (header.kind == 2 && header.requestSeq != null) {
                        writeFrame(output, encodeHeader(3, header.requestSeq, header.name, header.metadata), payload)
                    }
                }
            }
        } catch (_: Exception) {
        }
    }

    override fun close() {
        server.close()
        executor.shutdownNow()
    }

    private fun writeFrame(output: DataOutputStream, header: ByteArray, payload: ByteArray) {
        output.write(
            ByteBuffer.allocate(6 + header.size + payload.size)
                .putShort(header.size.toShort())
                .putInt(payload.size)
                .put(header)
                .put(payload)
                .array(),
        )
        output.flush()
    }

    private fun decodeHeader(header: ByteArray): Header {
        val buffer = ByteBuffer.wrap(header)
        val kind = buffer.get().toInt() and 0xff
        buffer.get()
        val flags = buffer.get().toInt() and 0xff
        val requestSeq = if ((flags and 0x01) == 0) null else buffer.long
        val nameLength = buffer.get().toInt() and 0xff
        val nameBytes = ByteArray(nameLength)
        buffer.get(nameBytes)
        val metadata = linkedMapOf<String, String>()
        if ((flags and 0x02) != 0) {
            val metadataLength = buffer.short.toInt() and 0xffff
            val metadataBuffer = ByteBuffer.wrap(buffer.readBytes(metadataLength))
            val count = metadataBuffer.get().toInt() and 0xff
            repeat(count) {
                val keyLength = metadataBuffer.get().toInt() and 0xff
                val key = String(metadataBuffer.readBytes(keyLength), StandardCharsets.UTF_8)
                val valueLength = metadataBuffer.short.toInt() and 0xffff
                val value = String(metadataBuffer.readBytes(valueLength), StandardCharsets.UTF_8)
                metadata[key] = value
            }
        }
        return Header(kind, requestSeq, String(nameBytes, StandardCharsets.UTF_8), metadata)
    }

    private fun encodeHeader(
        kind: Int,
        requestSeq: Long?,
        packetName: String,
        metadata: Map<String, String>,
    ): ByteArray {
        val name = packetName.toByteArray(StandardCharsets.UTF_8)
        val metadataBytes = encodeMetadata(metadata)
        val flags = (if (requestSeq == null) 0 else 0x01) or
            (if (metadataBytes.isEmpty()) 0 else 0x02)
        val buffer = ByteBuffer.allocate(
            3 + (if (requestSeq == null) 0 else 8) + 1 + name.size +
                (if (metadataBytes.isEmpty()) 0 else 2 + metadataBytes.size),
        )
        buffer.put(kind.toByte())
        buffer.put(0)
        buffer.put(flags.toByte())
        if (requestSeq != null) {
            buffer.putLong(requestSeq)
        }
        buffer.put(name.size.toByte())
        buffer.put(name)
        if (metadataBytes.isNotEmpty()) {
            buffer.putShort(metadataBytes.size.toShort())
            buffer.put(metadataBytes)
        }
        return buffer.array()
    }

    private fun encodeMetadata(metadata: Map<String, String>): ByteArray {
        if (metadata.isEmpty()) return ByteArray(0)
        var size = 1
        metadata.forEach { (key, value) ->
            size += 1 + key.toByteArray(StandardCharsets.UTF_8).size +
                2 + value.toByteArray(StandardCharsets.UTF_8).size
        }
        val buffer = ByteBuffer.allocate(size)
        buffer.put(metadata.size.toByte())
        metadata.forEach { (key, value) ->
            val keyBytes = key.toByteArray(StandardCharsets.UTF_8)
            val valueBytes = value.toByteArray(StandardCharsets.UTF_8)
            buffer.put(keyBytes.size.toByte())
            buffer.put(keyBytes)
            buffer.putShort(valueBytes.size.toShort())
            buffer.put(valueBytes)
        }
        return buffer.array()
    }

    private fun ByteBuffer.readBytes(length: Int): ByteArray =
        ByteArray(length).also { get(it) }

    private data class Header(
        val kind: Int,
        val requestSeq: Long?,
        val name: String,
        val metadata: Map<String, String>,
    )
}
