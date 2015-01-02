import zmq
c = zmq.Context()
s = c.socket(zmq.SUB)
s.connect("tcp://localhost:9110")
s.setsockopt(zmq.SUBSCRIBE, "")
while True:
    print s.recv().encode("hex")
    print s.recv().encode("hex")

