package systems.zlink.framework.streams;

import systems.zlink.contracts.messaging.Message;

public interface ZLinkSessionClient {
    ZLinkSessionSendCall send(Message message);

    ZLinkSessionReplyCall reply(Message message);
}
