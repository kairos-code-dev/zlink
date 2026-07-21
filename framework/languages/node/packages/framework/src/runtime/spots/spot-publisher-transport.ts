import type { Message } from '../../contracts/Common/Message';
import type { ZLinkPublishResult } from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkSpotPublisherClientTransport } from '../channels';

interface ZLinkRuntimeSpotPublisherManager {
  tryPublish(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkPublishResult;
  publish(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkPublishResult>;
}

export class ZLinkRuntimeSpotPublisherTransport implements ZLinkSpotPublisherClientTransport {
  constructor(private readonly manager: () => ZLinkRuntimeSpotPublisherManager | undefined) {}

  tryPublish(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkPublishResult {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('SPOT publisher runtime is not started.');
    }
    return manager.tryPublish(meshName, channelName, topic, packetName, event, metadata);
  }

  publish(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkPublishResult> {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('SPOT publisher runtime is not started.');
    }
    return manager.publish(meshName, channelName, topic, packetName, event, signal, metadata);
  }
}
