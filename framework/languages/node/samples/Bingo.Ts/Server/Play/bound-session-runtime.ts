const connector = require('../../../../../packages/stream-connector/dist');
const framework = require('../../../../../packages/framework/dist');
const { Message } = require('@zlink-systems/zlink');

class SampleBoundSessionRuntime {
  [key: string]: any;

  constructor() {
    this.delivered = [];
    this.deliveredSeq = 0;
    this.disconnected = [];
    this.runtime = new framework.ZLinkStreamBindingRuntime({
      messageFactory: {
        createTextMessage(payload) {
          return Message.from(payload);
        },
        createBinaryMessage(payload) {
          return Message.from(payload);
        }
      },
      transport: {
        send: async (actorId, message, options) => {
          const decoded = decodeJsonFrame(message);
          this.delivered.push({
            seq: ++this.deliveredSeq,
            actorId,
            token: options.bindingToken,
            packetName: options.packetName,
            payload: decoded.payload
          });
        },
        disconnect: async (actorId, options) => {
          this.disconnected.push({
            actorId,
            token: options.bindingToken
          });
        }
      }
    });
  }

  createBoundSession(actorId) {
    return this.runtime.createBoundSession(actorId);
  }

  async bind(actorRef, sessionId) {
    const context = this.runtime.createSessionContext(fakeStream(sessionId));
    const actor = await context.actors.bind(actorRef);
    return { actor, context, sessionId };
  }

  unbind(binding) {
    this.runtime.unbind(binding.actor.actorId, binding.context, binding.actor.bindingToken);
  }

  deliveredFor(actorId, afterSeq = 0) {
    const delivered = this.delivered.filter((entry) => entry.actorId === actorId && entry.seq > afterSeq);
    return {
      delivered,
      nextSeq: delivered.at(-1)?.seq ?? afterSeq
    };
  }
}

function fakeStream(sessionId) {
  return {
    sessionId,
    routingId: `rid-${sessionId}`,
    async close() {},
    write() {
      return true;
    }
  };
}

function decodeJsonFrame(message) {
  const frame = connector.ZlinkStreamFrameCodec.decode(message.toBytes());
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  return {
    header,
    payload: JSON.parse(Buffer.from(frame.payload).toString())
  };
}

export { SampleBoundSessionRuntime };
