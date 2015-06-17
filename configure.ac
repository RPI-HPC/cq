AC_INIT([cq],[0.5])
AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CC

AC_GNU_SOURCE

PKG_CHECK_MODULES([PROTOBUF_C], [libprotobuf-c >= 1.0.0])
PKG_CHECK_MODULES([MUNGE], [munge])
PKG_CHECK_MODULES([LIBZMQ], [libzmq])

AC_CHECK_PROG([PROTOC], [protoc-c], [protoc-c])
AS_IF([test "x${PROTOC}" == "x"],
          [AC_MSG_ERROR([ProtoBuf compiler "protoc" not found.])])

AC_OUTPUT([Makefile])
