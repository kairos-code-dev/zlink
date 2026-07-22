// Generated from service-wire-v1.schema.json. Do not edit.
package systems.zlink.framework.runtime.protocol;

public final class ServiceWireConstants {
    public static final int MAGIC_0 = 90;
    public static final int MAGIC_1 = 77;
    public static final int WIRE_MAJOR = 1;
    public static final String REQUIRED_CAPABILITY = "framework-service-v11";
    public static final int COMMAND_HELLO = 1;
    public static final int COMMAND_ADMIT = 2;
    public static final int COMMAND_REJECT = 3;
    public static final int COMMAND_UPDATE = 4;
    public static final int COMMAND_LIVENESS_PROBE = 5;
    public static final int COMMAND_LIVENESS_ACK = 6;
    public static final int COMMAND_NODE_SEND = 16;
    public static final int COMMAND_NODE_REQUEST = 17;
    public static final int COMMAND_CHANNEL_SEND = 18;
    public static final int COMMAND_CHANNEL_REQUEST = 19;
    public static final int COMMAND_REPLY = 20;
    public static final int COMMAND_SPOT_SEND = 21;
    public static final int COMMAND_SPOT_REQUEST = 22;
    public static final int COMMAND_LOGICAL_MULTICAST = 23;
    public static final int COMMAND_ACTOR_SEND = 24;
    public static final int COMMAND_ACTOR_REQUEST = 25;
    public static final int COMMAND_ACTOR_LOOKUP = 26;
    public static final int COMMAND_ACTOR_DESTROY = 27;
    public static final int COMMAND_ACTOR_JOIN = 28;
    public static final int COMMAND_ACTOR_LEFT = 29;
    public static final int COMMAND_TRANSFER_READY = 30;
    public static final int COMMAND_TRANSFER_DATA = 31;
    public static final int COMMAND_TRANSFER_ACK = 32;
    public static final int COMMAND_REPLY_RELAY = 33;
    public static final int COMMAND_TRANSFER_SEAL = 34;
    public static final int COMMAND_TRANSFER_COMPLETE = 35;
    public static final int COMMAND_BOUND_SESSION_SEND = 36;
    public static final int COMMAND_ACTOR_JOINED = 37;
    public static final int COMMAND_BOUND_SESSION_BIND = 38;
    public static final int COMMAND_INSTANCE_SPOT = 39;
    public static final int COMMAND_TRANSFER_PREPARE = 40;
    public static final int COMMAND_TRANSFER_RESERVED = 41;
    public static final int COMMAND_SESSION_TRANSFER_SEAL = 42;
    public static final int COMMAND_SESSION_TRANSFER_SEALED = 43;
    public static final int COMMAND_SESSION_TRANSFER_ROUTE = 44;
    public static final int COMMAND_SESSION_TRANSFER_ROUTED = 45;
    public static final int COMMAND_REPLY_RELAY_ACK = 46;
    public static final int FLAG_METADATA = 1;
    public static final int FLAG_BOUND_SESSION = 2;
    public static final int FLAG_SOURCE_SPOT_RID = 4;
    public static final int FLAG_EXTENSION = 8;
    private ServiceWireConstants() {}
}
