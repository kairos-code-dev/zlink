package main

import (
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

	endpoint := samplecommon.UniqueTCP("stream-callback")
	samplecommon.Must(server.Bind(endpoint))

	done := make(chan error, 1)
	samplecommon.Must(server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		done <- server.SendTo(received.RoutingID(), zlink.SendFlagsNone, samplecommon.Message("hello-stream"))
	}))

	conn := samplecommon.DialEndpoint(endpoint)
	defer conn.Close()

	sent := "hello-stream"
	_, err = conn.Write([]byte(sent))
	samplecommon.Must(err)

	buffer := make([]byte, len(sent))
	_, err = io.ReadFull(conn, buffer)
	samplecommon.Must(err)
	samplecommon.Must(<-done)

	fmt.Printf("[stream/callback] send: %q -> recv: %q\n", sent, string(buffer))
}
