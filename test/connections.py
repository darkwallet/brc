import zmq
import struct
c = zmq.Context()
s = c.socket(zmq.SUB)
s.connect("tcp://localhost:9112")
s.setsockopt(zmq.SUBSCRIBE, "")
while True:
    print "Connections:", struct.unpack("<Q", s.recv())[0]

