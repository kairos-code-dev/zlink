import type { Message } from '../../contracts/Common/Message';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkSpotPublisherClientTransport } from '../channels';

interface ZLinkRuntimeSpotPublisherManager {
  publish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    signal?: AbortSignal
  ): void;
}

export class ZLinkRuntimeSpotPublisherTransport implements ZLinkSpotPublisherClientTransport {
  constructor(private readonly manager: () => ZLinkRuntimeSpotPublisherManager | undefined) {}

  publish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    signal?: AbortSignal
  ): void {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('SPOT publisher runtime is not started.');
    }
    return manager.publish(channelName, topic, packetName, event, signal);
  }
}
