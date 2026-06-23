import type { ZLinkMessage, ZlinkStreamHeader } from '../Common';

export interface ZLinkSessionPacketHandler<TSessionContext> {
  handle(context: TSessionContext, header: ZlinkStreamHeader, payload: ZLinkMessage): Promise<void>;
}

export interface ZLinkSessionPacketDispatcher<TSessionContext> {
  dispatch(context: TSessionContext, header: ZlinkStreamHeader, payload: ZLinkMessage): Promise<void>;
}
