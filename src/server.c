#include "cq.pb-c.h"

#include <errno.h>
#include <munge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zmq.h>

#define WORKER_IPC "tcp://127.0.0.1:12425"

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

static int process_msg(void *sock, unsigned char *exit_status)
{
	int ret;
	CqReq *req;

	zmq_msg_t msg;

	void *buf;

	size_t len;

	ret = zmq_msg_init(&msg);
	if (ret < 0)
		return -1;

	ret = zmq_msg_recv(&msg, sock, 0);
	if (ret < 0) {
		zmq_msg_close(&msg);
		if (errno != EINTR) {
			fprintf(stderr, "Failed to receive message\n");
			return -1;
		}
		return -2;
	}

	len = zmq_msg_size(&msg);
	buf = zmq_msg_data(&msg);

	printf("Got message (%zu)\n", len);

	req = cq_req__unpack(NULL, len, buf);
	if (req == NULL) {
		fprintf(stderr, "Failed to unpack message\n");
		zmq_msg_close(&msg);
		return -1;
	}

	printf("New OP\n");
	ret = run(req->command, req->n_args, req->args, req->n_env, req->env, exit_status);

	cq_req__free_unpacked(req, NULL);

	zmq_msg_close(&msg);

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

	zmq_msg_close(&msg);
}

static void signal_handler(int signal)
{
	return;
}

static int worker()
{
	unsigned char exit_status;

	int ret;

	void *ctx = zmq_ctx_new();
	void *sock = zmq_socket(ctx, ZMQ_REP);

	zmq_connect(sock, WORKER_IPC);

	while (1) {
		exit_status = 1;
		ret = process_msg(sock, &exit_status);

		if (ret < 0) {
			if (ret == -2)
				break;
			printf("Failed to receive/process message\n");
		}

		send_response(sock, ret, exit_status);
	}

	printf("Worker shutting down...\n");

	ret = zmq_close(sock);
	if (ret < 0)
		fprintf(stderr, "Failed to close socket\n");

	ret = zmq_ctx_destroy(ctx);
	if(ret < 0)
		fprintf(stderr, "Failed to stop ZMQ\n");

	return 0;
}

static pid_t spawn_worker()
{
	pid_t pid;

	if ((pid = fork()) < 0) {
		return -1;
	}

	if (pid == 0) {
		int ret = worker();
		_exit(ret);
	}

	return pid;
}

int main(int argc, char *argv[])
{
	int rc = 1;
	int ret;

	signal(SIGINT, signal_handler);

	int c;
	char ep[28];
	const char *host = "0.0.0.0";
	char *ohost = NULL;
	int port = 48005;

	void *ctx = NULL;
	void *fe = NULL;
	void *be = NULL;

	while ((c = getopt (argc, argv, "h:p:")) != -1)
		switch (c) {
			case 'h':
				ohost = strdup(optarg);
				break;
			case 'p':
				port = atoi(optarg);
		}

	pid_t pid = spawn_worker();
	printf("Spawned worker w/ pid %d\n", pid);

	if (ohost != NULL)
		host = ohost;

	snprintf(ep, 28, "tcp://%s:%d", host, port);
	if (ohost != NULL)
		free(ohost);

	ctx = zmq_ctx_new();
	if (ctx == NULL)
		goto finished;

	fe = zmq_socket(ctx, ZMQ_ROUTER);
	if (fe == NULL)
		goto finished;

	be = zmq_socket(ctx, ZMQ_DEALER);
	if (be == NULL)
		goto finished;

	ret = zmq_bind(fe, ep);
	if (ret < 0) {
		fprintf(stderr, "Unable to bind socket\n");
		goto finished;
	}

	zmq_bind(be, WORKER_IPC);

	zmq_pollitem_t items [] = {
		{ fe, 0, ZMQ_POLLIN, 0 },
		{ be,  0, ZMQ_POLLIN, 0 }
	};

	printf("Waiting for messages...\n");

	void *cred;
	void *buf;
	int len;

	while (1) {
		zmq_msg_t message;
		zmq_msg_t out;
		ret = zmq_poll(items, 2, -1);

		if (ret < 0)
			if (errno == EINTR) {
				break;
			}

		if (items[0].revents & ZMQ_POLLIN) {
			while (1) {
				zmq_msg_init(&message);
				zmq_msg_recv(&message, fe, 0);
				int more = zmq_msg_more(&message);

				if (!more) {
					cred = zmq_msg_data(&message);

					munge_err_t err = munge_decode(cred, NULL, &buf, &len, NULL, NULL);
					if (err != EMUNGE_SUCCESS)
						fprintf(stderr, "Munge failed to decode\n");

					zmq_msg_init_data(&out, buf, len, free_buf, NULL);
					zmq_msg_send(&out, be, 0);
					zmq_msg_close(&out);
				} else {
					zmq_msg_send(&message, be, more? ZMQ_SNDMORE: 0);
				}

				zmq_msg_close(&message);
				if (!more)
					break;
			}
		}
		if (items[1].revents & ZMQ_POLLIN) {
			while (1) {
				zmq_msg_init(&message);
				zmq_msg_recv(&message, be, 0);
				int more = zmq_msg_more(&message);
				zmq_msg_send(&message, fe, more? ZMQ_SNDMORE: 0);
				zmq_msg_close(&message);
				if (!more)
					break;
			}
		}
	}

	rc = 0;

	printf("Shutting down\n");

finished:
	printf("Stopping workers...\n");

	pid_t w;
	int status;
	do {
		w = waitpid(pid, &status, 0);
		if (w == -1) {
			fprintf(stderr, "Unable to wait\n");
			break;
		}
	} while (!WIFEXITED(status) && !WIFSIGNALED(status));

	printf("Finishing cleanup\n");
	ret = zmq_close(fe);
	if (ret < 0)
		fprintf(stderr, "Failed to close frontend socket\n");

	ret = zmq_close(be);
	if (ret < 0)
		fprintf(stderr, "Failed to close backend socket\n");

	ret = zmq_ctx_destroy(ctx);
	if(ret < 0)
		fprintf(stderr, "Failed to stop ZMQ\n");

	return rc;
}
