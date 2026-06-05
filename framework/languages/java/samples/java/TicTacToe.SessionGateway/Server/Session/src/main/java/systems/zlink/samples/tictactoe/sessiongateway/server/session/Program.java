package systems.zlink.samples.tictactoe.sessiongateway.server.session;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        SessionServerHostFactory.start(args);
    }
}
