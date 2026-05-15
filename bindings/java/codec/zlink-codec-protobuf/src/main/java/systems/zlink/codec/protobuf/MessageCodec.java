package systems.zlink.codec.protobuf;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


interface MessageCodec<T extends com.google.protobuf.Message> {
    systems.zlink.contracts.Message toMessage(T value);

    T fromMessage(systems.zlink.contracts.Message message);
}
