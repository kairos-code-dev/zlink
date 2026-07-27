import type { ZLinkHandlerDelegate } from './ZLinkHandlerDelegate';
import type { ZLinkMessageContext } from './Contexts';

export interface ZLinkHandlerFilter {
  invoke(
    context: ZLinkMessageContext,
    next: ZLinkHandlerDelegate,
    signal?: AbortSignal
  ): Promise<unknown>;
}
