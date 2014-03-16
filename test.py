import zmq
c = zmq.Context()
s = c.socket(zmq.PUSH)
s.connect("tcp://localhost:9009")
s.send("asbc")

