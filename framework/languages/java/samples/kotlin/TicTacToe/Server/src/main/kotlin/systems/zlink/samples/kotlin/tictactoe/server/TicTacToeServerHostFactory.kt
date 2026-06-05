package systems.zlink.samples.kotlin.tictactoe.server

import org.springframework.context.ConfigurableApplicationContext
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServerHostFactory
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServerHostFactory

object TicTacToeServerHostFactory {
    fun start(settings: SampleSettings): AutoCloseable {
        var play: ConfigurableApplicationContext? = null
        var api: ConfigurableApplicationContext? = null
        try {
            play = PlayServerHostFactory.start(settings)
            api = ApiServerHostFactory.start(settings)
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
