package systems.zlink.samples.deliverydispatch.server.courier;

public record CourierOptions(String courierId, String mode) {
    public static CourierOptions fromArgs(String[] args) {
        String courierId = readOption(args, "--courier");
        if (courierId == null) {
            courierId = System.getenv("DELIVERYDISPATCH_COURIER_ID");
        }
        if (courierId == null) {
            courierId = "courier-a";
        }
        String mode = readOption(args, "--mode");
        if (mode == null) {
            mode = System.getenv("DELIVERYDISPATCH_COURIER_MODE");
        }
        if (mode == null) {
            mode = "accept";
        }
        return new CourierOptions(courierId, mode);
    }

    private static String readOption(String[] args, String name) {
        for (int index = 0; index + 1 < args.length; index++) {
            if (name.equals(args[index])) {
                return args[index + 1];
            }
        }
        return null;
    }
}
