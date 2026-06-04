package systems.zlink.samples.tictactoe.sessiongateway;

import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchClient;
import systems.zlink.samples.tictactoe.sessiongateway.client.SessionActorDispatchClientOptions;

public final class TicTacToeSessionGatewaySample {
    private TicTacToeSessionGatewaySample() {
    }

    public static void main(String[] args) throws Exception {
        SessionActorDispatchClient client = new SessionActorDispatchClient();
        try (AutoCloseable registry =
                 systems.zlink.samples.tictactoe.sessiongateway.server.registry.Program.start();
             AutoCloseable api =
                 systems.zlink.samples.tictactoe.sessiongateway.server.api.Program.start();
             AutoCloseable play =
                 systems.zlink.samples.tictactoe.sessiongateway.server.play.Program.start();
             AutoCloseable session =
                 systems.zlink.samples.tictactoe.sessiongateway.server.session.Program.start()) {
            client.runReconnectScenario(SessionActorDispatchClientOptions.defaults());
        }

        System.out.println("TicTacToe.SessionGateway sample self-check passed");
    }
}
