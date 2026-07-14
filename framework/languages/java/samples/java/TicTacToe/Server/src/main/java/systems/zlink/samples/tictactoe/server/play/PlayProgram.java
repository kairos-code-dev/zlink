package systems.zlink.samples.tictactoe.server.play;

import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;

public final class PlayProgram {
    private PlayProgram() {
    }

    public static void main(String[] args) {
        PlayServerApplication.run(SampleSettings.configPath(args));
    }
}
