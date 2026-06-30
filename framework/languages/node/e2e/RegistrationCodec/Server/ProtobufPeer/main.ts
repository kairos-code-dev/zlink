import { startProtobufPeer } from './protobuf-peer-host-factory';

startProtobufPeer(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
