AM_CPPFLAGS=-I$(builddir)/src

dist_noinst_DATA = src/cq.proto

bin_PROGRAMS=cq_client cq_server

cq_client_SOURCES=src/client.c src/cq.pb-c.c
cq_client_CFLAGS=$(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS) $(MUNGE_CFLAGS)
cq_client_LDADD=$(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS) $(MUNGE_LIBS)

cq_server_SOURCES=src/server.c src/cq.pb-c.c
cq_server_CFLAGS=$(LIBZMQ_CFLAGS) $(PROTOBUF_C_CFLAGS) $(MUNGE_CFLAGS)
cq_server_LDADD=$(LIBZMQ_LIBS) $(PROTOBUF_C_LIBS) $(MUNGE_LIBS)

MOSTLYCLEANFILES=src/cq.pb-c.c src/cq.pb-c.h

BUILT_SOURCES=src/cq.pb-c.h

%.pb-c.c %.pb-c.h: %.proto
	$(PROTOC) --proto_path=$(srcdir) --c_out=$(builddir) $^
