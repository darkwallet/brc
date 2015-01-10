import zmq
c = zmq.Context()
s = c.socket(zmq.SUB)
s.connect("tcp://localhost:9110")
s.setsockopt(zmq.SUBSCRIBE, "")
while True:
    # Tx hash
    print s.recv().encode("hex")
    # Number of success
    print s.recv().encode("hex")
    # Number of failure
    print s.recv().encode("hex")

