package systems.zlink.samples.tictactoe.sessiongateway.client;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        new SessionActorDispatchClient().runReconnectScenario(
            SessionActorDispatchClientOptions.defaults());
        System.out.println("TicTacToe.SessionGateway sample self-check passed");
    }
}
