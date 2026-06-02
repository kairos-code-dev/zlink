const connector = require('../../packages/stream-connector/dist');
const framework = require('../../packages/framework/dist');

class SampleBoundSessionRuntime {
  constructor() {
    this.delivered = [];
    this.disconnected = [];
    this.runtime = new framework.ZLinkStreamBindingRuntime({
      messageFactory: {
        createTextMessage(payload) {
          return simpleMessage(Buffer.from(payload));
        },
        createBinaryMessage(payload) {
          return simpleMessage(payload);
        }
      },
      transport: {
        send: async (actorId, message, options) => {
          const decoded = decodeJsonFrame(message);
          this.delivered.push({
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

function simpleMessage(bytes) {
  const payload = Buffer.from(bytes);
  return {
    data() {
      return payload;
    },
    toBytes() {
      return Buffer.from(payload);
    },
    copy() {
      return simpleMessage(payload);
    },
    size() {
      return payload.length;
    },
    isEmpty() {
      return payload.length === 0;
    },
    getString(encoding = 'utf8') {
      return payload.toString(encoding);
    },
    close() {}
  };
}

module.exports = { SampleBoundSessionRuntime };
