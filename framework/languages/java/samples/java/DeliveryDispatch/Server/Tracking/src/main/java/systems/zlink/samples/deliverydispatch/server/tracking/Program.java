package systems.zlink.samples.deliverydispatch.server.tracking;

import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        SampleTopology.configure(args);
        TrackingServerApplication.run(new String[0]);
    }
}
