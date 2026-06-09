import {
  RequiredZlinkStreamConnectorOptions,
  ZlinkStreamConnection,
  ZlinkStreamTransportFactory
} from '../../Contracts';
import { connectNodeStream, inferTransport } from './NodeSocketConnector';

export { inferTransport };

export class NodeStreamTransportFactory implements ZlinkStreamTransportFactory {
  async connect(options: RequiredZlinkStreamConnectorOptions, signal?: AbortSignal): Promise<ZlinkStreamConnection> {
    return await connectNodeStream(options, signal);
  }
}
