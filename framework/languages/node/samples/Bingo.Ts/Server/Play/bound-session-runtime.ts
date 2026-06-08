const connector = require('../../../../../packages/stream-connector/dist');
const framework = require('../../../../../packages/framework/dist');
const { Message } = require('@zlink-systems/zlink');

type BoundActorRef = {
  actorId: string;
};

type DeliveredFrame = {
  seq: number;
  actorId: string;
  token: string;
  packetName: string;
  payload: unknown;
};

type BindingRecord = {
  actor: {
    actorId: string;
    bindingToken: string;
  };
  context: unknown;
};

class SampleBoundSessionRuntime {
  [key: string]: any;

  constructor() {
    this.delivered = [];
    this.deliveredSeq = 0;
    this.disconnected = [];
    this.waiters = [];
    this.runtime = new framework.ZLinkStreamBindingRuntime({
      messageFactory: {
        createTextMessage(payload: string): any {
          return Message.from(payload);
        },
        createBinaryMessage(payload: Buffer): any {
          return Message.from(payload);
        }
      },
      transport: {
        send: async (actorId: string, message: any, options: { bindingToken: string; packetName: string }) => {
          const decoded = decodeJsonFrame(message);
          this.delivered.push({
            seq: ++this.deliveredSeq,
            actorId,
            token: options.bindingToken,
            packetName: options.packetName,
            payload: decoded.payload
          });
          this.notifyWaiters();
        },
        disconnect: async (actorId: string, options: { bindingToken: string }) => {
          this.disconnected.push({
            actorId,
            token: options.bindingToken
          });
        }
      }
    });
  }

  createBoundSession(actorId: string): any {
    return this.runtime.createBoundSession(actorId);
  }

  async bind(actorRef: BoundActorRef, sessionId: string): Promise<{ actor: unknown; context: unknown; sessionId: string }> {
    const context = this.runtime.createSessionContext(fakeStream(sessionId));
    const actor = await context.actors.bind(actorRef);
    return { actor, context, sessionId };
  }

  unbind(binding: BindingRecord): void {
    this.runtime.unbind(binding.actor.actorId, binding.context, binding.actor.bindingToken);
  }

  deliveredFor(actorId: string, afterSeq: number = 0): { delivered: DeliveredFrame[]; nextSeq: number } {
    const delivered = this.delivered.filter((entry) => entry.actorId === actorId && entry.seq > afterSeq);
    return {
      delivered,
      nextSeq: delivered.at(-1)?.seq ?? afterSeq
    };
  }

  async waitForDelivered(actorId: string, afterSeq: number = 0): Promise<{ delivered: DeliveredFrame[]; nextSeq: number }> {
    const ready = this.deliveredFor(actorId, afterSeq);
    if (ready.delivered.length > 0) {
      return ready;
    }
    return await new Promise((resolve) => {
      this.waiters.push({ actorId, afterSeq, resolve });
    });
  }

  notifyWaiters(): void {
    const pending = [];
    for (const waiter of this.waiters) {
      const ready = this.deliveredFor(waiter.actorId, waiter.afterSeq);
      if (ready.delivered.length === 0) {
        pending.push(waiter);
        continue;
      }
      waiter.resolve(ready);
    }
    this.waiters = pending;
  }
}

function fakeStream(sessionId: string): any {
  return {
    sessionId,
    routingId: `rid-${sessionId}`,
    async close(): Promise<void> {},
    write(): boolean {
      return true;
    }
  };
}

function decodeJsonFrame(message: any): { header: unknown; payload: unknown } {
  const frame = connector.ZlinkStreamFrameCodec.decode(message.toBytes());
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  return {
    header,
    payload: JSON.parse(Buffer.from(frame.payload).toString())
  };
}

export { SampleBoundSessionRuntime };
