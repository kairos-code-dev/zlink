import type { Message, ZlinkStreamHeader } from '../Common';

export interface ZLinkSessionPacketHandler<TSessionContext> {
  handle(context: TSessionContext, header: ZlinkStreamHeader, payload: Message): Promise<void>;
}

export interface ZLinkSessionPacketDispatcher<TSessionContext> {
  dispatch(context: TSessionContext, header: ZlinkStreamHeader, payload: Message): Promise<void>;
}
