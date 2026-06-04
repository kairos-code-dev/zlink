package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

class SessionActorDispatchClient {
    suspend fun run(options: SessionActorDispatchClientOptions): SessionActorDispatchClientResult {
        SessionActorDispatchPlayerClient.connect(options.xActorId, options.primaryStreamEndpoint).use { xClient ->
            SessionActorDispatchPlayerClient.connect(options.oActorId, options.reconnectStreamEndpoint).use { oClient ->
                val xAuth = xClient.authenticate()
                val oAuth = oClient.authenticate()
                val created = xClient.createMatch()

                xClient.close()
                SessionActorDispatchPlayerClient.connect(options.xActorId, options.reconnectStreamEndpoint).use { reconnectedXClient ->
                    val xReconnectAuth = reconnectedXClient.authenticate()
                    val xJoin = reconnectedXClient.join(created.matchId)
                    val oJoin = oClient.join(created.matchId)
                    val moves = listOf(
                        reconnectedXClient.placeMark(created.matchId, 0),
                        oClient.placeMark(created.matchId, 3),
                        reconnectedXClient.placeMark(created.matchId, 1),
                        oClient.placeMark(created.matchId, 4),
                        reconnectedXClient.placeMark(created.matchId, 2),
                    )

                    validateReconnect(options, xAuth, xReconnectAuth, xJoin.state)
                    validateFinalState(options, moves.last().state)
                    waitForNotifications(reconnectedXClient, oClient)
                    validateNotifications(reconnectedXClient, oClient)

                    val notifications =
                        reconnectedXClient.turnChangedNotifications() +
                            oClient.turnChangedNotifications() +
                            reconnectedXClient.opponentJoinedNotifications() +
                            oClient.opponentJoinedNotifications() +
                            reconnectedXClient.gameEndedNotifications() +
                            oClient.gameEndedNotifications()
                    return SessionActorDispatchClientResult(
                        xAuth,
                        oAuth,
                        xReconnectAuth,
                        created,
                        xJoin,
                        oJoin,
                        moves,
                        notifications,
                        reconnectedXClient.notificationCount() + oClient.notificationCount(),
                    )
                }
            }
        }
    }

    suspend fun runReconnectScenario(options: SessionActorDispatchClientOptions) {
        run(options).writeTo()
    }

    private fun validateFinalState(
        options: SessionActorDispatchClientOptions,
        state: TicTacToeState,
    ) {
        require(state.status == "Won" && state.winnerActorId == options.xActorId && state.board == "XXXOO....") {
            "Unexpected final state: board=${state.board}, status=${state.status}, winner=${state.winnerActorId}"
        }
    }

    private fun validateReconnect(
        options: SessionActorDispatchClientOptions,
        firstAuth: AuthenticateRes,
        reconnectAuth: AuthenticateRes,
        reconnectedJoinState: TicTacToeState,
    ) {
        require(
            firstAuth.actorId == options.xActorId &&
                reconnectAuth.actorId == options.xActorId &&
                reconnectedJoinState.xActorId == options.xActorId,
        ) {
            "Reconnect did not recover the same actor binding."
        }
    }

    private suspend fun waitForNotifications(
        xClient: SessionActorDispatchPlayerClient,
        oClient: SessionActorDispatchPlayerClient,
    ) {
        val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(3)
        while (System.nanoTime() < deadline) {
            xClient.dispatch()
            oClient.dispatch()
            if (hasRequiredNotificationSmoke(xClient, oClient)) {
                return
            }
            kotlinx.coroutines.delay(10)
        }
        error("Timed out waiting for push notifications. ${notificationCounts(xClient, oClient)}")
    }

    private fun validateNotifications(
        xClient: SessionActorDispatchPlayerClient,
        oClient: SessionActorDispatchPlayerClient,
    ) {
        require(hasRequiredNotificationSmoke(xClient, oClient)) {
            "Session actor dispatch push notifications did not reach both actors. ${notificationCounts(xClient, oClient)}"
        }
    }

    private fun hasRequiredNotificationSmoke(
        xClient: SessionActorDispatchPlayerClient,
        oClient: SessionActorDispatchPlayerClient,
    ): Boolean = xClient.notificationCount() > 0 && oClient.notificationCount() > 0

    private fun notificationCounts(
        xClient: SessionActorDispatchPlayerClient,
        oClient: SessionActorDispatchPlayerClient,
    ): String =
        "x(turn=${xClient.turnChangedNotifications().size}, joined=${xClient.opponentJoinedNotifications().size}, ended=${xClient.gameEndedNotifications().size}), " +
            "o(turn=${oClient.turnChangedNotifications().size}, joined=${oClient.opponentJoinedNotifications().size}, ended=${oClient.gameEndedNotifications().size})"
}

data class SessionActorDispatchClientResult(
    val xAuthentication: AuthenticateRes,
    val oAuthentication: AuthenticateRes,
    val xReconnectAuthentication: AuthenticateRes,
    val createdMatch: CreateMatchRes,
    val xJoin: JoinMatchRes,
    val oJoin: JoinMatchRes,
    val moves: List<PlaceMarkRes>,
    val notifications: List<String>,
    val notificationCount: Int,
) {
    fun writeTo() {
        val finalState = moves.last().state
        println("auth=${xAuthentication.actorId},${oAuthentication.actorId}")
        println("created=${createdMatch.matchId}, reconnect=${xReconnectAuthentication.actorId}")
        println("players=X:${xJoin.state.xActorId}, O:${oJoin.state.oActorId}")
        println("final=${finalState.board}, status=${finalState.status}, winner=${finalState.winnerActorId}")
        println("notifications=$notificationCount")
    }
}
