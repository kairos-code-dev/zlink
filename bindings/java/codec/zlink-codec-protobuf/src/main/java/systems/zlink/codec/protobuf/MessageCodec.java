package systems.zlink.codec.protobuf;

import systems.zlink.contracts.messaging.Message;
interface MessageCodec<T extends com.google.protobuf.Message> {
    systems.zlink.contracts.messaging.Message toMessage(T value);

    T fromMessage(systems.zlink.contracts.messaging.Message message);
}
