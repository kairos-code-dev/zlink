package systems.zlink.codec.protobuf;

interface MessageCodec<T extends com.google.protobuf.Message> {
    systems.zlink.Message toMessage(T value);

    T fromMessage(systems.zlink.Message message);
}
