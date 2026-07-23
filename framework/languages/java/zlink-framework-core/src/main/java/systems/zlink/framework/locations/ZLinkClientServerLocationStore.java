package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkClientServerLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateClientServer(
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteStatus> removeClientServer(
        ZLinkClientServerServerDescriptorKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkLocationPage<ZLinkClientServerServerDescriptor>>
        listClientServers(
            String channelName,
            ZLinkPageRequest page);
}
