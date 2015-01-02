default:
	g++ main.cpp btcnet.cpp util.cpp $(shell pkg-config --cflags --libs libczmq++ libbitcoin) -lboost_filesystem -o brc

