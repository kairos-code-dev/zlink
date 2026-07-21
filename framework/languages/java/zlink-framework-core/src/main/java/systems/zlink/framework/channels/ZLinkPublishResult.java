package systems.zlink.framework.channels;

public record ZLinkPublishResult(
    ZLinkSubmitStatus status,
    ZLinkLogicalMulticastDetail detail) {
}
