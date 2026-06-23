import type { ZLinkMessage } from '../Common';
import type { ZLinkSessionDispatchContext } from './IZLinkSession';

export interface ZLinkSessionPacketHandler<TSessionContext> {
  handle(context: TSessionContext, dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void>;
}

export interface ZLinkSessionPacketDispatcher<TSessionContext> {
  dispatch(context: TSessionContext, dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void>;
}
