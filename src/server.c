#include "cq.pb-c.h"

#include <errno.h>
#include <munge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zmq.h>

static int run(const char *cmd, int argc, char *argv[], int envc, char *envp[], unsigned char *exit_status)
{
	int status;
	pid_t pid;

	if (cmd == NULL || argv == NULL || envp == NULL)
		return -1;

	int i;

	printf("Env:\n");
	for(i=0;i<envc-1;i++)
		printf("  %s\n", envp[i]);
	free(envp[envc-1]);
	envp[envc-1] = NULL;

	printf("Running: %s", cmd);
	for(i=1;i<argc-1;i++)
		printf(" %s", argv[i]);
	free(argv[argc-1]);
	argv[argc-1] = NULL;
	printf("\n");

	if ((pid = fork()) < 0) {
		return -1;
	}

	if (pid == 0) {
		execvpe(cmd, argv, envp);
		_exit(127);
	}

	do {
		if (waitpid(pid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		} else {
			/* TODO: many conditions here, see man waitpid */
			*exit_status = WEXITSTATUS(status);
			return 0;
		}
	} while(1);
}

static int decode_request(void *sock, void **buf) {
	int ret;
	zmq_msg_t msg;

	char *cred;

	int len;

	ret = zmq_msg_init(&msg);
	if (ret < 0)
		return -1;

	ret = zmq_msg_recv(&msg, sock, 0);
	if (ret < 0) {
		if (errno != EINTR) {
			fprintf(stderr, "Failed to receive message\n");
			return -1;
		}
		return -2;
	}

	cred = zmq_msg_data(&msg);

	munge_err_t err = munge_decode(cred, NULL, buf, &len, NULL, NULL);
	if (err != EMUNGE_SUCCESS) {
		fprintf(stderr, "Munge failed to decode\n");
		return -1;
	}

	zmq_msg_close(&msg);

	return len;
}

static int process_msg(void *buf, int len, unsigned char *exit_status)
{
	int ret;
	CqReq *req;

	printf("Got message (%d)\n", len);
	
	req = cq_req__unpack(NULL, len, buf);
	if (req == NULL) {
		fprintf(stderr, "Failed to unpack message\n");
		return -1;
	}

	printf("New OP\n");
	ret = run(req->command, req->n_args, req->args, req->n_env, req->env, exit_status);

	cq_req__free_unpacked(req, NULL);

	return ret;
}

static void free_buf(void *buf, void *hint)
{
	free(buf);
}

static void send_response(void *sock, int internal_status, int exit_status)
{
	void *buf;
	int len;

	CqRep rep = CQ_REP__INIT;
	zmq_msg_t msg;
	zmq_msg_init(&msg);

	rep.exit_status = exit_status;
	rep.internal_status = internal_status;

	len = cq_rep__get_packed_size(&rep);
	buf = malloc(len);
	cq_rep__pack(&rep, buf);

	zmq_msg_init_data(&msg, buf, len, free_buf, NULL);

	zmq_msg_send(&msg, sock, 0);
}

static void signal_handler(int signal)
{
	return;
}

int main(int argc, char *argv[])
{
	unsigned char exit_status;
	int rc = 1;
	int ret;

	void *buf;

	signal(SIGINT, signal_handler);

	int c;
	char ep[28];
	const char *host = "0.0.0.0";
	char *ohost = NULL;
	int port = 48005;

	while ((c = getopt (argc, argv, "h:p:")) != -1)
		switch (c) {
			case 'h':
				ohost = strdup(optarg);
				break;
			case 'p':
				port = atoi(optarg);
		}

	if (ohost != NULL)
		host = ohost;

	snprintf(ep, 28, "tcp://%s:%d", host, port);
	if (ohost != NULL)
		free(ohost);

	void *ctx = zmq_ctx_new();
	void *sock = zmq_socket (ctx, ZMQ_REP);

	ret = zmq_bind (sock, ep);
	if (ret < 0) {
		fprintf(stderr, "Unable to bind socket\n");
		goto finished;
	}

	printf("Waiting for messages...\n");

	while (1) {
		ret = decode_request(sock, &buf);
		if (ret == -2)
			break;

		if (ret < 0) {
			printf("Failed to receive/decode message\n");
		} else {
			ret = process_msg(buf, ret, &exit_status);
			free(buf);
		}

		send_response(sock, ret, exit_status);
	}

	printf("Shutting down\n");

	rc = 0;

finished:
	ret = zmq_close(sock);
	if (ret < 0)
		fprintf(stderr, "Failed to close socket\n");

	ret = zmq_ctx_destroy(ctx);
	if(ret < 0)
		fprintf(stderr, "Failed to stop ZMQ\n");

	return rc;
}
