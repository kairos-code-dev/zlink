const net = require('node:net');
const connector = require('../../../packages/stream-connector/dist');
const encoder = new TextEncoder();

async function main() {
  const { host, port } = parseEndpoint(process.env.STREAMING_CLIENT_ENDPOINT);
  const server = net.createServer((socket) => {
    let buffer = Buffer.alloc(0);
    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      const packet = tryReadFrame(buffer);
      if (packet === undefined) {
        return;
      }
      buffer = buffer.subarray(packet.length);
      const frame = connector.ZlinkStreamFrameCodec.decode(packet.bytes);
      const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
      if (header.kind === connector.ZlinkStreamMessageKind.Request) {
        socket.write(connector.ZlinkStreamFrameCodec.encode(
          connector.ZlinkStreamHeaderCodec.encode({
            kind: connector.ZlinkStreamMessageKind.Response,
            codec: connector.ZlinkStreamCodec.Json,
            flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
            requestSeq: header.requestSeq,
            name: 'JoinAccepted',
            metadata: connector.ZlinkStreamMetadataMap.empty
          }),
          encoder.encode(JSON.stringify({ accepted: true, playerId: JSON.parse(Buffer.from(frame.payload).toString()).playerId }))
        ));
        socket.write(connector.ZlinkStreamFrameCodec.encode(
          connector.ZlinkStreamHeaderCodec.encode({
            kind: connector.ZlinkStreamMessageKind.Send,
            codec: connector.ZlinkStreamCodec.Json,
            flags: connector.ZlinkStreamHeaderFlags.None,
            name: 'ServerNotice',
            metadata: connector.ZlinkStreamMetadataMap.empty
          }),
          encoder.encode(JSON.stringify({ message: 'welcome' }))
        ));
      }
    });
  });
  await listen(server, port, host);
  process.stdout.write(`${JSON.stringify({ event: 'ready', endpoint: process.env.STREAMING_CLIENT_ENDPOINT })}\n`);
  await new Promise((resolve) => {
    process.once('SIGINT', resolve);
    process.once('SIGTERM', resolve);
  });
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
}

function parseEndpoint(endpoint) {
  const value = endpoint.replace('tcp://', '');
  const [host, port] = value.split(':');
  return { host, port: Number(port) };
}

function listen(server, port, host) {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolve);
  });
}

function tryReadFrame(buffer) {
  if (buffer.length < 6) {
    return undefined;
  }
  const headerLength = (buffer[0] << 8) | buffer[1];
  const payloadLength = buffer[2] * 0x1000000 + ((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]);
  const length = 6 + headerLength + payloadLength;
  if (buffer.length < length) {
    return undefined;
  }
  return { length, bytes: buffer.subarray(0, length) };
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
