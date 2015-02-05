all: sendfile recvfile

sendfile: sendfile.cc
	g++ -Wall -o sendfile sendfile.cc

recvfile: recvfile.cc
	g++ -Wall -o recvfile recvfile.cc

clean:
	rm -f sendfile
	rm -f recvfile
