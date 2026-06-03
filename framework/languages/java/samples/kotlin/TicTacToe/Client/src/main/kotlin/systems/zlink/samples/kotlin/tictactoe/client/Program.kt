package systems.zlink.samples.kotlin.tictactoe.client

import kotlinx.coroutines.runBlocking
import systems.zlink.framework.ZLinkFramework

fun main(args: Array<String>) = runBlocking {
    val clientOptions = TicTacToeClientArguments.parse(args)
    ZLinkFramework.start { options ->
        options.codecs().addJson()
        options.addClientServerChannel("tictactoe-api") { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints ->
                    endpoints.connect(clientOptions.apiEndpoint)
                }
            }
        }
    }.use { framework ->
        val result = TicTacToeClient(framework.client()).run(clientOptions)
        result.writeTo(System.out)
    }
}
