import type {
  DealerSocket,
  Message,
  PubSocket
} from '@zlink-systems/zlink';
import { ZLinkConfigurationException } from '../configuration';
import {
  decodeChannelReply,
  encodeChannelEnvelopeParts,
  ZLinkChannelMessageKind
} from './channel-envelope';
import { throwIfAborted } from '../abort';
import {
  appendParts,
  submitRequestOperation
} from './channel-multipart';
import type { ZLinkChannelClientTransport } from './channel-transports';

export class ZLinkDealerChannelClientTransport implements ZLinkChannelClientTransport {
  constructor(
    private readonly dealer: DealerSocket,
    private readonly publisher?: PubSocket
  ) {}

  send(channelName: string, packetName: string | undefined, message: Message, signal?: AbortSignal): void {
    throwIfAborted(signal);
    appendParts(
      this.dealer.send(),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, channelName, packetName, message)
    ).submit();
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const operation = appendParts(
      this.dealer.request(),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, channelName, packetName, request, timeoutMs)
    );
    if (timeoutMs !== undefined) {
      operation.timeout(timeoutMs);
    }
    const reply = await submitRequestOperation(operation, 'channel request');
    return decodeChannelReply<TReply>(reply);
  }

  publish(channelName: string, topic: string, packetName: string | undefined, event: Message, signal?: AbortSignal): void {
    throwIfAborted(signal);
    if (this.publisher === undefined) {
      throw new ZLinkConfigurationException('Channel publisher runtime is not started.');
    }
    appendParts(
      this.publisher.publish(topic),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Publish, channelName, packetName, event, undefined, topic)
    ).submit();
  }
}
