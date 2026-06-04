package systems.zlink.samples.bingo.server.session;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        SessionServerHostFactory.start(args);
    }
}
