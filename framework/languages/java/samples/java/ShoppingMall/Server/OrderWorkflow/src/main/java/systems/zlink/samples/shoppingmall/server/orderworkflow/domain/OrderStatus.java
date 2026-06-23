package systems.zlink.samples.shoppingmall.server.orderworkflow.domain;

public final class OrderStatus {
    public static final String Created = "Created";
    public static final String InventoryReserved = "InventoryReserved";
    public static final String PaymentAuthorized = "PaymentAuthorized";
    public static final String Confirmed = "Confirmed";
    public static final String Failed = "Failed";

    private OrderStatus() {
    }
}
