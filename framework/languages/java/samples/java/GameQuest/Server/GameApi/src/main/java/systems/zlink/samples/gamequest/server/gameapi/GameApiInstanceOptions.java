package systems.zlink.samples.gamequest.server.gameapi;

/** Parsed {@code --api} selector for a GameApi process ({@code api-a}/{@code api-b}). */
public record GameApiInstanceOptions(String apiName) {
    public static GameApiInstanceOptions fromArgs(String[] args) {
        String apiName = "api-a";
        for (int i = 0; i < args.length - 1; i++) {
            if ("--api".equals(args[i])) {
                apiName = args[i + 1];
            }
        }
        return new GameApiInstanceOptions(apiName);
    }
}
