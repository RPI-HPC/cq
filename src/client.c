#include "utilizer.pb-c.h"

#include <limits.h>
#include <munge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zmq.h>

static void free_buf(void *buf, void *hint)
{
	free(buf);
}

static int send_request(void *sock, int argc, char *argv[])
{
	int ret;
	size_t i;
	void *buf = NULL;
	size_t len;

	UtilReq req = UTIL_REQ__INIT;

	req.command = argv[1];

	req.n_args = argc;
	req.args = malloc (sizeof(char *) * req.n_args);

	for (i=0;i<req.n_args;i++)
		req.args[i] = argv[i+1];
	req.args[req.n_args-1] = "";

	len = util_req__get_packed_size(&req);
	printf("Packed size %zu\n", len);
	buf = malloc(len);
	util_req__pack(&req, buf);

	free(req.args);

	char *cred;
	munge_err_t err = munge_encode (&cred, NULL, buf, len);
	if (err != EMUNGE_SUCCESS) {
		fprintf(stderr, "Munge encode failure\n");
		return -1;
	}

	free(buf);

	zmq_msg_t msg;

	ret = zmq_msg_init_data (&msg, cred, strlen(cred)+1, free_buf, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to create message\n");
		return ret;
	}

	ret = zmq_msg_send(&msg, sock, 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to send message\n");
		zmq_msg_close(&msg);
		return ret;
	}

	return 0;
}

static int recv_response(void *sock)
{
	int ret;
	void *buf = NULL;
	size_t len;

	zmq_msg_t msg;

	ret = zmq_msg_init(&msg);
	if (ret < 0)
		return ret;

	ret = zmq_msg_recv(&msg, sock, 0);
	if (ret < 0)
		return ret;

	buf = zmq_msg_data(&msg);
	len = zmq_msg_size(&msg);

	UtilRep *rep = util_rep__unpack(NULL, len, buf);

	if (rep->internal_status < 0) {
		printf("Op failed\n");
	} else {
		printf("Op complete: %u\n", rep->exit_status);
	}

	util_rep__free_unpacked(rep, NULL);

	return 0;
}

int main(int argc, char *argv[])
{
	int rc = EXIT_FAILURE;
	int ret;

	if (argc < 2) {
		void usage();
		exit(EXIT_FAILURE);
	}

	void *ctx = zmq_ctx_new();

	void *sock = zmq_socket(ctx, ZMQ_REQ);
	ret = zmq_connect(sock, "tcp://127.0.0.1:48005");
	if (ret < 0) {
		fprintf(stderr, "Unable to connect\n");
		goto finished;
	}

	ret = send_request(sock, argc, argv);
	if (ret < 0)
		goto finished;

	printf("Going into background while waiting for response\n");

	// TODO: background
	//

	ret = recv_response(sock);
	if (ret < 0)
		goto finished;

	rc = EXIT_SUCCESS;

finished:
	zmq_close(sock);
	zmq_term(ctx);

	return rc;
}
