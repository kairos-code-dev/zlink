package main

import (
	"bytes"
	"fmt"
	"io"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer ctx.Close()

	server, err := ctx.StreamSocket()
	samplecommon.Must(err)
	defer server.Close()

	endpoint := samplecommon.UniqueTCP("stream-recv")
	samplecommon.Must(server.Bind(endpoint))

	conn := samplecommon.DialEndpoint(endpoint)
	defer conn.Close()

	sent := "hello-stream"
	_, err = conn.Write([]byte(sent))
	samplecommon.Must(err)

	received, err := server.Recv(zlink.RecvFlagsNone)
	samplecommon.Must(err)
	defer received.Close()
	part, err := received.SinglePartOrError()
	samplecommon.Must(err)
	if !bytes.Equal(part.Data(), []byte(sent)) {
		samplecommon.Must(fmt.Errorf("unexpected payload %q", string(part.Data())))
	}

	samplecommon.Must(server.SendTo(received.RoutingID(), zlink.SendFlagsNone, samplecommon.Message(sent)))

	buffer := make([]byte, len(sent))
	_, err = io.ReadFull(conn, buffer)
	samplecommon.Must(err)
	fmt.Printf("[stream/recv] send: %q -> recv: %q\n", sent, string(buffer))
}
