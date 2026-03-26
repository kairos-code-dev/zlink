import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.Spot(ctx) as spot:
            spot.subscribe(b"room:lobby")
            spot.publish(b"room:lobby", [b"hello"])
            print("spot recv surface prepared", spot.recv)


if __name__ == "__main__":
    main()
