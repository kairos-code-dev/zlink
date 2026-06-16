package systems.zlink.samples.shoppingmallcheckout.server.commerceapi;

import systems.zlink.samples.shoppingmallcheckout.server.configuration.SampleNames;

/** Parsed {@code --instance} selector for a CommerceApi process. */
public record CommerceApiInstanceOptions(String instanceId) {
    public static CommerceApiInstanceOptions fromArgs(String[] args) {
        String instanceId = SampleNames.ApiInstanceA;
        for (int i = 0; i < args.length - 1; i++) {
            if ("--instance".equals(args[i])) {
                instanceId = args[i + 1];
            }
        }
        return new CommerceApiInstanceOptions(instanceId);
    }

    public static String peerInstance(String instanceId) {
        return SampleNames.ApiInstanceB.equals(instanceId)
            ? SampleNames.ApiInstanceA
            : SampleNames.ApiInstanceB;
    }
}
