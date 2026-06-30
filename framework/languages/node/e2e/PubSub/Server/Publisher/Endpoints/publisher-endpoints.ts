import type { ZLinkFanoutClient } from '@zlink-systems/framework';
import { PacketNames, PubSubNames, type EventMsg, type MissingEventMsg } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createPublisherEndpoints(
  fanout: ZLinkFanoutClient,
  evidence: EvidenceStore,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'publisher', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } },
    {
      method: 'POST',
      path: '/publish/event',
      handle: async (body) => {
        const request = body as PublishRequest;
        const event: EventMsg = { runId: request.runId, sequence: Number(request.sequence), value: request.value };
        fanout.publishToChannel(PubSubNames.channel, request.topic, event)
          .packetName(PacketNames.eventMsg)
          .submit();
        return { status: 'published', topic: request.topic, runId: request.runId, sequence: event.sequence };
      }
    },
    {
      method: 'POST',
      path: '/publish/missing',
      handle: async (body) => {
        const request = body as PublishRequest;
        const event: MissingEventMsg = { runId: request.runId, sequence: Number(request.sequence), value: request.value };
        fanout.publishToChannel(PubSubNames.channel, request.topic, event)
          .packetName(PacketNames.missingEventMsg)
          .submit();
        return { status: 'published', topic: request.topic, runId: request.runId, sequence: event.sequence };
      }
    }
  ];
}

interface PublishRequest {
  readonly topic: string;
  readonly runId: string;
  readonly sequence: number;
  readonly value: string;
}
