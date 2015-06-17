#include "cq.pb-c.h"

#include <limits.h>
#include <munge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zmq.h>

static void free_buf(void *buf, void *hint)
{
	free(buf);
}

static int send_request(void *sock, int argc, char *argv[], int envc, char *envp[])
{
	int ret;
	size_t i;
	void *buf = NULL;
	size_t len;

	CqReq req = CQ_REQ__INIT;

	req.command = argv[0];

	req.n_args = argc + 1;
	req.args = malloc(sizeof(char *) * req.n_args);

	req.n_env = envc + 1;
	req.env = malloc(sizeof(char *) * req.n_env);

	for (i=0;i<req.n_args-1;i++)
		req.args[i] = argv[i];
	req.args[req.n_args-1] = "";

	for (i=0;i<req.n_env-1;i++)
		req.env[i] = envp[i];
	req.env[req.n_env-1] = "";

	len = cq_req__get_packed_size(&req);
	printf("Packed size %zu\n", len);
	buf = malloc(len);
	cq_req__pack(&req, buf);

	free(req.args);
	free(req.env);

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

static int run_post(const char *post, int internal_status, int exit_status)
{
	pid_t pid;

	char buf1[16];
	char buf2[16];

	snprintf(buf1, 16, "%d", internal_status);
	snprintf(buf2, 16, "%d", exit_status);

	if ((pid = fork()) < 0) {
		return -1;
	}

	if (pid == 0) {
		if ((pid = fork()) < 0) {
			_exit(1);
		}

		if (pid == 0) {
			execl(post, post, buf1, buf2, (char *)NULL);
			_exit(127);
		}

		_exit(0);
	}

	return 0;
}

static int recv_response(void *sock, const char *post)
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

	CqRep *rep = cq_rep__unpack(NULL, len, buf);

	if (rep == NULL)
		return -1;

	int internal_status = rep->internal_status;
	int exit_status = rep->exit_status;

	cq_rep__free_unpacked(rep, NULL);

	if (internal_status < 0) {
		printf("Op failed\n");
	} else {
		printf("Op complete: %u\n", exit_status);
	}

	if (post != NULL)
		run_post(post, internal_status, exit_status);

	return 0;
}

static void usage(const char *name)
{
	printf("Usage: %s \n", name);
}

int main(int argc, char *argv[])
{
	int rc = EXIT_FAILURE;
	int ret;
	int c, i;
	char ep[28];
	const char *host = "127.0.0.1";
	char *ohost = NULL;
	char *post = NULL;
	int port = 48005;

	int envc = 0;
	char **envp = NULL;

	while ((c = getopt (argc, argv, "E:h:p:P:")) != -1)
		switch (c) {
			case 'E':
				envc++;

				envp = realloc(envp, sizeof(char *) * envc);
				if (envp == NULL) {
					fprintf(stderr, "OOM\n");
					goto finished;
				}

				envp[envc-1] = strdup(optarg);
				break;
			case 'h':
				ohost = strdup(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'P':
				post = strdup(optarg);
		}

	if (ohost != NULL)
		host = ohost;

	snprintf(ep, 28, "tcp://%s:%d", host, port);
	if (ohost != NULL)
		free(ohost);

	void *ctx = zmq_ctx_new();
	void *sock = zmq_socket(ctx, ZMQ_REQ);

	ret = zmq_connect(sock, ep);
	if (ret < 0) {
		fprintf(stderr, "Unable to connect to %s\n", ep);
		goto finished;
	}

	ret = send_request(sock, argc-optind, argv+optind, envc, envp);
	if (ret < 0)
		goto finished;

	printf("Going into background while waiting for response\n");

	// TODO: background
	//

	ret = recv_response(sock, post);
	if (ret < 0)
		goto finished;

	rc = EXIT_SUCCESS;

finished:
	if (post != NULL)
		free(post);

	if (envp != NULL) {
		for (i=0; i<envc; i++) {
			free(envp[i]);
		}
		free(envp);
	}

	zmq_close(sock);
	zmq_term(ctx);

	return rc;
}
