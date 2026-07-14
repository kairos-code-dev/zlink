package systems.zlink.samples.tictactoe.server.api;

import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;

public final class ApiProgram {
    private ApiProgram() {
    }

    public static void main(String[] args) {
        ApiServerApplication.run(SampleSettings.configPath(args));
    }
}
