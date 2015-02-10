# Distributed File Transfer Protocol
This protocol tries to have a new implementation for transferring files over a computer network between one node to another node in a multiconnected environment. The protocol is responsible for ensuring data is delivered in a specified order, without duplicates, missing data, or errors. There are two sides of a coin
- The fact that the receiver port has to be known can be similar to an agreement between two parties i.e. sender and receiver so that the communication can only happen there instead of a standard port designated for the FTP protocol communication.
- In case of large scale implementation it'd involve polling the ports to find required port so that connection to the sender node can be established and the information served.

## Implementation
For sender side, the packet format is designed as follow.

+------------------------------+<br/>
|           DFTPHash           |<br/>
+---------------+--------------+<br/>
| serial number | payload size |<br/>
+---------------+--------------+<br/>
|            payload           |<br/>
+------------------------------+<br/>

DFTP Hash is used to ensure the packet content is correct.
- If serial number == 0, payload is the file name. The first packet contains file name and file size, storing in the payload.
- If serial number is greater than 0, payload is file content. The sender first send a packet containing to receiver, then continue to send file contents.

For receiver side, the recieving packet format is very simple.

+---------------+<br/>
| serial number |<br/>
+---------------+<br/>

Receiver will receive a packet containing file name and file size. After the receiver checks there is no error (DFTP Hash, serial number, payload size) in the packet, it will reply with a serial number. The receiver creates the file on the current directory. If receiver receives all data, it will say goodbye to sender by sending extra ACK bits.

This protocol supports unlimited file sizes by using mmap(). At most 8 Megabytes file contents are mapped into memory at once. When this part is transferred to the recevier, these data unmapped from memory by munmap() and next 8 Megabytes file contents are mapped by again until all the file contents are successfully sent to the receiver.

## Test
sendfile runs on some domain and recvfile runs on some other domain who's IP in the sample can be something like 1.1.1.1, which IP address is 1.1.1.1. md5sum is used to validate file integrity.

User two commands below to launch recvfile and sendfile after setting on an agreed port in this case 18011 is as follows:
```
$ ./recvfile -p 18011
$ ./sendfile -r 1.1.1.1:18011 -f test
```
