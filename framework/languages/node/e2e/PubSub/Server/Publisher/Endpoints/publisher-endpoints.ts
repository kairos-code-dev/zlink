import type { ZLinkDrainControl, ZLinkFanoutClient } from '@zlink-systems/framework';
import { EventMsg, MissingEventMsg, PubSubNames } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createPublisherEndpoints(
  fanout: ZLinkFanoutClient,
  evidence: EvidenceStore,
  drain: ZLinkDrainControl,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'publisher', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } },
    { method: 'POST', path: '/admin/drain', handle: () => drain.drain(30_000) },
    {
      method: 'POST',
      path: '/publish/event',
      handle: async (body) => {
        const request = body as PublishRequest;
        const event = new EventMsg(request.runId, Number(request.sequence), request.value);
        fanout.publish(PubSubNames.channel, request.topic, event)
          .submit();
        return { status: 'published', topic: request.topic, runId: request.runId, sequence: event.sequence };
      }
    },
    {
      method: 'POST',
      path: '/publish/missing',
      handle: async (body) => {
        const request = body as PublishRequest;
        const event = new MissingEventMsg(request.runId, Number(request.sequence), request.value);
        fanout.publish(PubSubNames.channel, request.topic, event)
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
