import { Injectable } from '@nestjs/common';
import { ZLinkPacket } from '@zlink-systems/framework';
import type {
  ZLinkHandlerContext,
  ZLinkSpotPacketHandler,
  ZLinkSpotRequestHandler
} from '@zlink-systems/framework';
import type {
  CloseConversationRes,
  JoinConversationRes,
  SendChatMessageRes
} from '../../../../../../../Shared/Contracts/messages';
import type { ConversationSpot } from '../conversation-spot';

class JoinConversationAtSpotReq { constructor(readonly actorId: string) {} }
class SendChatMessageAtSpotReq { constructor(readonly actorId: string, readonly text: string) {} }
class SetTypingAtSpotMsg { constructor(readonly actorId: string, readonly isTyping: boolean) {} }
class CloseConversationAtSpotReq { constructor(readonly actorId: string) {} }

@Injectable()
@ZLinkPacket('JoinConversationAtSpotReq')
class JoinConversationAtSpotHandler
  implements ZLinkSpotRequestHandler<ConversationSpot, JoinConversationAtSpotReq, JoinConversationRes> {
  async handle(spot: ConversationSpot, request: JoinConversationAtSpotReq, _context: ZLinkHandlerContext): Promise<JoinConversationRes> {
    return { state: spot.join(request.actorId) };
  }
}

@Injectable()
@ZLinkPacket('SendChatMessageAtSpotReq')
class SendChatMessageAtSpotHandler
  implements ZLinkSpotRequestHandler<ConversationSpot, SendChatMessageAtSpotReq, SendChatMessageRes> {
  async handle(spot: ConversationSpot, request: SendChatMessageAtSpotReq, _context: ZLinkHandlerContext): Promise<SendChatMessageRes> {
    return await spot.sendChat(request.actorId, request.text);
  }
}

@Injectable()
@ZLinkPacket('SetTypingAtSpotMsg')
class SetTypingAtSpotHandler implements ZLinkSpotPacketHandler<ConversationSpot, SetTypingAtSpotMsg> {
  async handle(spot: ConversationSpot, message: SetTypingAtSpotMsg, _context: ZLinkHandlerContext): Promise<void> {
    await spot.setTyping(message.actorId, message.isTyping);
  }
}

@Injectable()
@ZLinkPacket('CloseConversationAtSpotReq')
class CloseConversationAtSpotHandler
  implements ZLinkSpotRequestHandler<ConversationSpot, CloseConversationAtSpotReq, CloseConversationRes> {
  async handle(spot: ConversationSpot, request: CloseConversationAtSpotReq, _context: ZLinkHandlerContext): Promise<CloseConversationRes> {
    return { state: await spot.close(request.actorId) };
  }
}

export {
  CloseConversationAtSpotHandler,
  CloseConversationAtSpotReq,
  JoinConversationAtSpotHandler,
  JoinConversationAtSpotReq,
  SendChatMessageAtSpotHandler,
  SendChatMessageAtSpotReq,
  SetTypingAtSpotHandler,
  SetTypingAtSpotMsg
};
