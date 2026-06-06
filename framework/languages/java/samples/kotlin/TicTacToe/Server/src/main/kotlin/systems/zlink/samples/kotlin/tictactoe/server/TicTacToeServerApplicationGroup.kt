package systems.zlink.samples.kotlin.tictactoe.server

import org.springframework.context.ConfigurableApplicationContext
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServerApplication
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServerApplication

object TicTacToeServerApplicationGroup {
    fun run(settings: SampleSettings): AutoCloseable {
        var play: ConfigurableApplicationContext? = null
        var api: ConfigurableApplicationContext? = null
        try {
            play = PlayServerApplication.run(settings)
            api = ApiServerApplication.run(settings)
            return ServerHost(play, api)
        } catch (error: RuntimeException) {
            api?.close()
            play?.close()
            throw error
        }
    }

    private class ServerHost(
        private val play: ConfigurableApplicationContext?,
        private val api: ConfigurableApplicationContext?,
    ) : AutoCloseable {
        override fun close() {
            api?.close()
            play?.close()
        }
    }
}
