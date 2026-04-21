package dev.kairoscode.zlink.codec.protobuf;

interface MessageCodec<T extends com.google.protobuf.Message> {
    dev.kairoscode.zlink.Message toMessage(T value);

    T fromMessage(dev.kairoscode.zlink.Message message);
}
