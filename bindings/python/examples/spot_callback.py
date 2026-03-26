import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.Spot(ctx) as spot:
            def on_message(message):
                print(message.topic.decode("utf-8"), message.to_bytes_list())

            spot.set_handler(on_message)
            spot.publish(b"room:lobby", [b"hello"])
            print("spot callback surface prepared")


if __name__ == "__main__":
    main()
