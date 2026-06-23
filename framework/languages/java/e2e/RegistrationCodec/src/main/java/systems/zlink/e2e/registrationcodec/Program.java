package systems.zlink.e2e.registrationcodec;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        switch (Env.get("ZLINK_JAVA_E2E_ROLE", "client")) {
            case "server" -> ServerApplication.run(args);
            case "invalid-server" -> InvalidServerApplication.run(args);
            case "client" -> ClientApplication.run(args);
            default -> throw new IllegalArgumentException(
                "unknown role " + Env.get("ZLINK_JAVA_E2E_ROLE"));
        }
    }
}
