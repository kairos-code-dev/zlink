export * from './Contracts';
export { ZlinkStreamFrameCodec } from './Runtime/Protocol/ZlinkStreamFrameCodec';
export { ZlinkStreamHeaderCodec } from './Runtime/Protocol/ZlinkStreamHeaderCodec';
export {
  DefaultZlinkStreamConnector,
} from './Runtime/ZlinkStreamConnector';
import { DefaultZlinkStreamConnector } from './Runtime/ZlinkStreamConnector';
import type { ZlinkStreamConnectorOptions } from './Contracts';

export const zlinkStreamConnectorFactory = {
  create(options: ZlinkStreamConnectorOptions): DefaultZlinkStreamConnector {
    return new DefaultZlinkStreamConnector(options);
  }
};
