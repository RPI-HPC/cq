bin_PROGRAMS=client server

client_SOURCES=src/client.c src/utilizer.pb-c.c
client_LDADD=-lzmq -lprotobuf-c

server_SOURCES=src/server.c src/utilizer.pb-c.c
server_LDADD=-lzmq -lprotobuf-c
