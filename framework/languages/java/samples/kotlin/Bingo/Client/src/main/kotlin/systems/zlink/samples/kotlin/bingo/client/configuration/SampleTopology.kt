package systems.zlink.samples.kotlin.bingo.client.configuration

import java.nio.file.Files
import java.nio.file.Path
import java.util.Properties

object SampleTopology {
    lateinit var SessionAStreamEndpoint: String
    lateinit var SessionBStreamEndpoint: String

    fun configure(args: Array<String>) {
        require(args.size == 2 && args[0] == "--config" && args[1].isNotBlank()) {
            "Usage: Client --config <path>"
        }
        val properties = Properties().also { values ->
            Files.newBufferedReader(Path.of(args[1])).use(values::load)
        }
        SessionAStreamEndpoint = value(properties, "sessionAStreamEndpoint", "tcp://127.0.0.1:47114")
        SessionBStreamEndpoint = value(properties, "sessionBStreamEndpoint", "tcp://127.0.0.1:47125")
    }

    private fun value(properties: Properties, name: String, fallback: String): String =
        properties.getProperty(name)?.takeIf(String::isNotBlank) ?: fallback
}
