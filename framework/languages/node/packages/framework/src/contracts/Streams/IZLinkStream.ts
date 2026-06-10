import type { Message, RoutingId } from '../Common';

export interface ZLinkStream {
  readonly sessionId: string;
  readonly routingId?: RoutingId;
  readonly localAddr?: string;
  readonly remoteAddr?: string;
  write(payload: Message, flags?: number): boolean;
  close(signal?: AbortSignal): Promise<void>;
}
