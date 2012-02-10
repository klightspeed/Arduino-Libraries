/*
 * ard_tcp.c
 *
 *  Created on: May 27, 2010
 *      Author: mlf by Metodo2 srl
 */

//#define _APP_DEBUG_
#include "lwip/opt.h"

#include "lwip/mem.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/inet_chksum.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

#include "ard_tcp.h"
#include "ard_spi.h"
#include "timer.h"
#include "util.h"

#include "getopt.h"
#include "ard_utils.h"
#include "debug.h"

unsigned int startTime = 0;

/**
 * Clean up and free the ttcp structure
 */
static void ard_tcp_destroy(struct ttcp* ttcp) {
	err_t err = ERR_OK;
	DUMP_TCP_STATE(ttcp);

	if (ttcp->tpcb) {
		tcp_arg(ttcp->tpcb, NULL);
		tcp_sent(ttcp->tpcb, NULL);
		tcp_recv(ttcp->tpcb, NULL);
		tcp_err(ttcp->tpcb, NULL);
		err = tcp_close(ttcp->tpcb);
		INFO_TCP("Closing tpcb: state:0x%x err:%d\n", ttcp->tpcb->state, err);
	}

	if (ttcp->lpcb) {
		tcp_arg(ttcp->lpcb, NULL);
		tcp_accept(ttcp->lpcb, NULL);
		tcp_close(ttcp->lpcb);
		INFO_TCP("Closing lpcb: state:0x%x err:%d\n", ttcp->lpcb->state, err);
	}

	if (ttcp->upcb) {
		udp_disconnect(ttcp->upcb);
		udp_remove(ttcp->upcb);
	}

	if (ttcp->payload)
		free(ttcp->payload);

	free(ttcp);
}

/**
 * Clean up and free the ttcp structure
 */
static void ard_tcp_abort(struct ttcp* ttcp) {

	INFO_TCP("Abort ttcb:%p tpcb:%p lpcb:%p\n", ttcp, ttcp->tpcb, ttcp->lpcb);
	if (ttcp->tpcb) {
		tcp_arg(ttcp->tpcb, NULL);
		tcp_sent(ttcp->tpcb, NULL);
		tcp_recv(ttcp->tpcb, NULL);
		tcp_err(ttcp->tpcb, NULL);
		tcp_abort(ttcp->tpcb);
	}

	if (ttcp->lpcb) {
		tcp_arg(ttcp->lpcb, NULL);
		tcp_accept(ttcp->lpcb, NULL);
		tcp_abort(ttcp->lpcb);
	}

	if (ttcp->upcb) {
		udp_disconnect(ttcp->upcb);
		udp_remove(ttcp->upcb);
	}

	if (ttcp->payload)
		free(ttcp->payload);

	free(ttcp);
}


/**
 * Invoked when transfer is done or aborted (non-zero result).
 */
static void ard_tcp_done(struct ttcp* ttcp, int result) {
//	if (result == 0)
//		ard_tcp_print_stats(ttcp);

	if (ttcp->done_cb)
		ttcp->done_cb(ttcp->opaque, result);

	ard_tcp_destroy(ttcp);
}

static void
tcp_timeout_cb(void *ctx);

/**
 * Only used in TCP mode. 
 * Will transmit a maximum of pbuf->tot_len bytes. 
 * Called upon connect and when there's space available in the TCP send window
 * 
 */
static void tcp_send_data(struct ttcp *ttcp) {
	err_t err;
	uint32_t len;

	len = ttcp->left;

	/* don't send more than we have in the payload */
	if (len > ttcp->buflen)
		len = ttcp->buflen;

	/* We cannot send more data than space available in the send
	 buffer. */
	if (len > tcp_sndbuf(ttcp->tpcb))
		len = tcp_sndbuf(ttcp->tpcb);

	do {
		err = tcp_write(ttcp->tpcb, ttcp->payload, len, 0);
		if (err == ERR_MEM)
			len /= 2;
	} while (err == ERR_MEM && len > 1);

	if (err == ERR_OK){
		tcp_output(ttcp->tpcb);
		ttcp->left -= len;
	}
	else
		WARN("TTCP [%p]: tcp_write failed\n", ttcp);
	//
	//        ttcp->tid = timer_sched_timeout_cb(0, TIMEOUT_ONESHOT,
	//                                           tcp_timeout_cb, ttcp);
}

/**
 * Only used in TCP mode.
 * Scheduled by tcp_send_data(). tcp_sent() is not used for performance reasons.
 */
static void tcp_timeout_cb(void *ctx) {
	struct ttcp *ttcp = ctx;

	if (ttcp->left > 0) {
		tcp_send_data(ttcp);
		if (ttcp->verbose) {
			printk(".");
			if (ttcp->print_cnt % 80 == 0)
				printk("\n");
			ttcp->print_cnt++;
		}
		return;
	}

	/* all sent - empty queue */
	if (ttcp->tpcb->snd_queuelen)
		ttcp->tid = timer_sched_timeout_cb(0, TIMEOUT_ONESHOT, tcp_timeout_cb,
				ttcp);
	else
		ard_tcp_done(ttcp, 0);
}

#if 0
/**
 * Only used in TCP mode.
 * Called by lwip when there is new space available in the TCP send buffer
 */
static err_t
tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
	struct ttcp *ttcp = arg;

	if (ttcp->left > 0) {
		tcp_send_data(ttcp);
		if (ttcp->verbose) {
			printk(".");
			if (ttcp->print_cnt % 80 == 0)
			printk("\n");
			ttcp->print_cnt++;
		}

	} else if (pcb->snd_queuelen == 0) {
		ard_tcp_done(ttcp, 0);
	}

	return ERR_OK;
}
#endif

/**
 * Only used in TCP mode.
 */
static err_t tcp_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
	struct ttcp* ttcp = arg;

	printk("TTCP [%p]: connect\n", ttcp);

	ttcp->start_time = timer_get_ms();
#if 0
	tcp_sent(tpcb, tcp_sent_cb);

	tcp_send_data(ttcp);
#endif
	return ERR_OK;
}

/** 
 * Only used in TCP mode.
 */
static void atcp_conn_err_cb(void *arg, err_t err) {
	struct ttcp* ttcp = arg;

	printk("TTCP [%p]: connection error:0x%x\n", ttcp, err);

	ttcp->tpcb = NULL; /* free'd by lwip upon return */
	int sock = getSock(ttcp);
	if (sock)
		clearMapSockTcp(sock);
	ard_tcp_done(ttcp, err);
}

/**
 * Only used in TCP mode.
 */
static err_t atcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p,
		err_t err) {
	struct ttcp* ttcp = arg;
	/* p will be NULL when remote end is done */
	if (p == NULL) {
		INFO_TCP("atcp_recv_cb p=NULL\n");
		ard_tcp_done(ttcp, 0);
		return ERR_OK;
	}
	DATA_LED_ON();
	/* for print_stats() */
	ttcp->recved += p->tot_len;

	if (ttcp->verbose) {
		INFO_TCP("Recv:%d\n",p->tot_len);
		DUMP(p->payload, p->tot_len);
		ttcp->print_cnt++;
	}

	insert_pBuf(p, ttcp->sock, (void*) pcb);
	pbuf_free(p);
	tcp_recved(pcb, p->tot_len);
	DATA_LED_OFF();
	return ERR_OK;
}

void ack_recved(void* pcb, int len) {
	// Comment the call because it is activated on atcp_recv_cb
	//tcp_recved(pcb, len);
}

static err_t atcp_poll(void *arg, struct tcp_pcb *pcb) {
	return ERR_OK;
}

/**
 * Only used in TCP mode.
 */
static err_t atcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
	struct ttcp* ttcp = arg;

	ttcp->tpcb = newpcb;
	tcp_recv(ttcp->tpcb, atcp_recv_cb);
	tcp_err(ttcp->tpcb, atcp_conn_err_cb);
	tcp_poll(ttcp->tpcb, atcp_poll, 4);

	INFO_TCP("ARD TCP [%p]: accept new [%p]\n", ttcp, newpcb);
	INFO_TCP("local:%d remote:%d state:%d\n", newpcb->local_port, newpcb->remote_port, newpcb->state);
	ttcp->start_time = timer_get_ms();
	return ERR_OK;
}

/**
 * Start TCP transfer.
 */
static int atcp_start(struct ttcp* ttcp) {
	err_t err = ERR_OK;
	ttcp->tpcb = tcp_new();
	if (ttcp->tpcb == NULL) {
		WARN("TTCP [%p]: could not allocate pcb\n", ttcp);
		return -1;
	}

	ttcp->payload = malloc(ttcp->buflen);
	if (ttcp->payload == NULL) {
		WARN("TTCP [%p]: could not allocate payload\n", ttcp);
		return -1;
	}

	tcp_arg(ttcp->tpcb, ttcp);

	if (ttcp->mode == TTCP_MODE_TRANSMIT) {
		tcp_err(ttcp->tpcb, atcp_conn_err_cb);
		tcp_recv(ttcp->tpcb, atcp_recv_cb);
		if (tcp_connect(ttcp->tpcb, &ttcp->addr, ttcp->port, tcp_connect_cb)
				!= ERR_OK) {
			WARN("TTCP [%p]: tcp connect failed\n", ttcp);
			atcp_conn_err_cb(ttcp, err);
			return -1;
		}

	} else {
		INFO_TCP("BEFORE BIND ttcp:%p lpcb:%p pcb:%p\n", ttcp, ttcp->lpcb, ttcp->tpcb);
		INFO_TCP("[tpcb]-local:%d remote:%d state:%d\n", ttcp->tpcb->local_port,
				ttcp->tpcb->remote_port, ttcp->tpcb->state);

		err = tcp_bind(ttcp->tpcb, IP_ADDR_ANY, ttcp->port);
		if (err != ERR_OK){
			WARN("TTCP [%p]: bind failed err=%d\n", ttcp, err);
			return -1;
		}

		ttcp->lpcb = tcp_listen(ttcp->tpcb);
		if (ttcp->lpcb == NULL) {
			WARN("TTCP [%p]: listen failed\n", ttcp);
			return -1;
		}
		if (ttcp->lpcb == ttcp->tpcb ) {
			WARN("TTCP [%p]: listen failed tpcb [%p] in listen mode\n", ttcp, ttcp->tpcb);
			return -1;
		}

		DUMP_TCP_STATE(ttcp);
		tcp_accept(ttcp->lpcb, atcp_accept_cb);
	}

	return 0;
}

static void
udp_send_data(struct ttcp* ttcp);

/**
 * Only used in UDP mode. Scheduled after data has been sent in udp_send_data()
 * if we have more data to send.
 */
static void udp_timeout_cb(void *ctx) {
	struct ttcp* ttcp = ctx;
	udp_send_data(ttcp);
}

static int udp_send_bytes(struct ttcp* ttcp, uint32_t len) {
	struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
	if (p == NULL) {
		WARN("TTCP [%p]: could not allocate pbuf\n", ttcp);
		return -1;
	}

	if (udp_send(ttcp->upcb, p) != ERR_OK) {
		WARN("TTCP [%p]: udp_send() failed\n", ttcp);
		pbuf_free(p);
		return -1;
	}

	pbuf_free(p);
	return 0;
}

/**
 * Only used in UDP mode. First call will send the start marker. When all 
 * ttcp data has been sent, a number of end markers will be sent. After
 * end marker transmission, this function will complete the ttcp process.
 */
static void udp_send_data(struct ttcp* ttcp) {
	/* send start marker first time */
	if (!ttcp->udp_started) {
		if (udp_send_bytes(ttcp, 4) == 0) {
			ttcp->udp_started = 1;
			ttcp->start_time = timer_get_ms();
		}
	}

	/* normal case */
	else if (ttcp->left) {
		/* send data */
		if (udp_send_bytes(ttcp, ttcp->buflen) == 0)
			ttcp->left -= ttcp->buflen;
	}

	/* end marker? */
	else if (ttcp->left == 0 && ttcp->udp_end_marker_left) {
		if (udp_send_bytes(ttcp, 4) == 0)
			ttcp->udp_end_marker_left--;
	}

	/* all end markers sent */
	else if (ttcp->left == 0) {
		ard_tcp_done(ttcp, 0);
		return;
	}

	ttcp->tid
			= timer_sched_timeout_cb(0, TIMEOUT_ONESHOT, udp_timeout_cb, ttcp);
}

/**
 * Only used in UDP mode. Will finalize the ttcp process when an end marker
 * is seen.
 */
static void udp_recv_cb(void *arg, struct udp_pcb *upcb, struct pbuf *p,
		struct ip_addr *addr, u16_t port) {
	struct ttcp* ttcp = arg;

	/* got start marker? we might lose this so if we get it just reset
	 * the timer
	 */
	if (!ttcp->udp_started && p->tot_len <= 4) {
		ttcp->start_time = timer_get_ms();
		ttcp->udp_started = 1;
		goto out;
	}

	/* after receiving at least 1 byte, check end marker
	 * don't check udp_started since we might have lost the start marker
	 */
	if (ttcp->recved && p->tot_len <= 4) {
		ard_tcp_done(ttcp, 0);
		goto out;
	}

	/* for print_stats() */
	ttcp->recved += p->tot_len;
	if (ttcp->verbose) {
		printk(".");
		if (ttcp->print_cnt % 80 == 0)
			printk("\n");
		ttcp->print_cnt++;
	}

	out: pbuf_free(p);
}

/**
 * Start UDP transfer.
 */
static int udp_start(struct ttcp* ttcp) {
	ttcp->udp_end_marker_left = 5;
	ttcp->upcb = udp_new();
	if (ttcp->upcb == NULL) {
		printk("TTCP [%p]: could not allocate pcb\n", ttcp);
		return -1;
	}

	if (ttcp->mode == TTCP_MODE_TRANSMIT) {
		if (udp_connect(ttcp->upcb, &ttcp->addr, ttcp->port) != ERR_OK) {
			printk("TTCP [%p]: udp connect failed\n", ttcp);
			return -1;
		}
		udp_send_data(ttcp);
	} else {
		udp_recv(ttcp->upcb, udp_recv_cb, ttcp);
	}

	return 0;
}

/**
 * Start a new ttcp transfer. It should be possible to call this function
 * multiple times in order to get multiple ttcp streams. done_cb() will be
 * invoked upon completion.
 * 
 */
int ard_tcp_start(struct ip_addr addr, uint16_t port, void *opaque,
		ard_tcp_done_cb_t *done_cb, int mode, uint16_t nbuf, uint16_t buflen,
		int udp, int verbose, uint8_t sock, void** _ttcp) {
	struct ttcp* ttcp;
	int status;

	if (mode != TTCP_MODE_TRANSMIT && mode != TTCP_MODE_RECEIVE) {
		WARN("TTCP [-]: invalid mode\n");
		return -1;
	}

	if (nbuf == 0) {
		WARN("TTCP [-]: invalid nbuf\n");
		return -1;
	}

	if (buflen == 0) {
		WARN("TTCP [-]: invalid buflen\n");
		return -1;
	}

	ttcp = calloc(1, sizeof(struct ttcp));
	if (ttcp == NULL) {
		WARN("TTCP [-]: could not allocate memory for ttcp\n");
		return -1;
	}

	ttcp->addr = addr;
	ttcp->port = port;
	ttcp->nbuf = nbuf;
	ttcp->mode = mode;
	ttcp->left = nbuf * buflen;
	ttcp->done_cb = done_cb;
	ttcp->opaque = opaque;
	ttcp->udp = udp;
	ttcp->verbose = verbose;
	ttcp->buflen = buflen;

	if (ttcp->udp)
		status = udp_start(ttcp);
	else
		status = atcp_start(ttcp);

	if (status) {
		WARN("Start server FAILED!\n");
		goto fail;
	}
	INFO_TCP("TTCP [%p-%p]: nbuf=%d, buflen=%d, port=%d (%s/%s)\n", ttcp,
			ttcp->tpcb, ttcp->nbuf, ttcp->buflen, ttcp->port, ttcp->udp ? "udp"
					: "tcp", ttcp->mode == TTCP_MODE_TRANSMIT ? "tx" : "rx");

	*_ttcp = (void*) ttcp;
	ttcp->sock = sock;
	ttcp->buff_sent = 1;

	return 0;

	fail: ard_tcp_abort(ttcp);
	return -1;
}

static void close_conn(struct ttcp *_ttcp) {
	ard_tcp_done(_ttcp, 0);
}

void ard_tcp_stop(void* ttcp) {
	struct ttcp* _ttcp = (struct ttcp*) ttcp;
	INFO_TCP("Closing connection...\n");
	DUMP_TCP_STATE(_ttcp);
	if ((_ttcp)&&(_ttcp->tpcb)&&(_ttcp->tpcb->state!=LAST_ACK)&&(_ttcp->tpcb->state!=CLOSED))
		close_conn(_ttcp);
}

uint8_t getStateTcp(void* p, bool client) {
	struct ttcp* _ttcp = (struct ttcp*) p;

	if ((_ttcp != NULL) && (_ttcp->tpcb != NULL)) {
		//DUMP_TCP_STATE(_ttcp);
		if (client)
			return _ttcp->tpcb->state;
		else
			return _ttcp->lpcb->state;
	} else {
		INFO_TCP("TCP not initialized ttcp:%p tpcb:%p lpcb:%p\n",
				_ttcp, ((_ttcp)?_ttcp->tpcb:0), ((_ttcp)?_ttcp->lpcb:0));
	}
	return CLOSED;
}

uint8_t isDataSent(void* p) {
	static int isDataSentCount = 0;
	struct ttcp* _ttcp = (struct ttcp*) p;
	if ((_ttcp != NULL) && (_ttcp->tpcb != NULL)) {
		//INFO_TCP("ttcp:%p tpcb:%p sent:%d\n",p, _ttcp->tpcb, _ttcp->buff_sent);
		bool dataSent = _ttcp->buff_sent;
		if (dataSent) isDataSentCount = 0;
		else ++isDataSentCount;
		if (isDataSentCount == 250)
			WARN("data not sent\n");
		return dataSent;
	} else {
		WARN("TCP null!\n");
	}

	return -1;
}

static err_t tcp_data_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
	struct ttcp *_ttcp;

	LWIP_UNUSED_ARG(len);

	_ttcp = arg;

	if (_ttcp->left > 0) {
		//send_data(pcb, hs);
		INFO_TCP("data left: %d", _ttcp->left );
	}
	if (_ttcp->buff_sent == 1)
		WARN("Previous packet already\n");
	_ttcp->buff_sent = 1;
	//INFO_TCP("%s: duration: %d\n", __FUNCTION__, timer_get_ms() - startTime);
	return ERR_OK;
}

int sendTcpData(void* p, uint8_t* buf, uint16_t len) {
	//INFO_TCP("buf:%p len:%d\n", buf, len);
	//DUMP(buf,len);

	//startTime = timer_get_ms();
	struct ttcp* _ttcp = (struct ttcp*) p;
	if ((_ttcp != NULL) && (_ttcp->tpcb != NULL) &&
			(buf != NULL) && (len != 0) && (_ttcp->payload != NULL)) {
		if (_ttcp->tpcb->state == ESTABLISHED ||
				_ttcp->tpcb->state == CLOSE_WAIT ||
				_ttcp->tpcb->state == SYN_SENT ||
				_ttcp->tpcb->state == SYN_RCVD) {
		_ttcp->buff_sent = 0;
		//pbuf_take(buf, len, _ttcp->);
		memcpy(_ttcp->payload, buf, len);
		_ttcp->left = len;
		tcp_sent(_ttcp->tpcb, tcp_data_sent);
		tcp_send_data(_ttcp);

		return WL_SUCCESS;
		}
	}
	//printk("Write failure _ttcp=%p _ttcp->tpcb=%p buf=%p len=%d\n", _ttcp, _ttcp->tpcb, buf, len);
	return WL_FAILURE;
}

char
		usage[] =
				"Usage: ttcp -t/-r [-options] host\n\
        -l      length of bufs written to network (default 1024)\n\
        -n      number of bufs written to network (default 1024)\n\
        -p      port number to send to (default 2000)\n\
        -u      udp\n\
        -v      verbose\n";

/**
 *
 */
cmd_state_t cmd_ttcp(int argc, char* argv[], void* ctx) {

	int c;
	int mode = TTCP_MODE_TRANSMIT;
	int verbose = 0;
	uint16_t buflen = 1024;
	uint16_t nbuf = 1024;
	uint16_t port = 2000;
	int udp = 0;
	struct ip_addr addr = { 0 };

	optind = 1;
	while ((c = getopt(argc, argv, "utrl:n:p:v")) != -1) {
		switch (c) {
		case 't':
			mode = TTCP_MODE_TRANSMIT;
			break;
		case 'r':
			mode = TTCP_MODE_RECEIVE;
			break;
		case 'l':
			buflen = atoi(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'n':
			nbuf = atoi(optarg);
			break;
		case 'u':
			udp = 1;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		}
	}

	if (mode == TTCP_MODE_TRANSMIT) {
		if (optind >= argc) {
			printk("%s", usage);
			return CMD_DONE;
		}

		addr = str2ip(argv[optind]);
		if (!addr.addr) {
			printk("%s", usage);
			return CMD_DONE;
		}
	}
	void* _ttcp = NULL;
	if (ard_tcp_start(addr, port, NULL, NULL, mode, nbuf, buflen, udp, verbose,
			0, &_ttcp))
		return CMD_DONE;

	return CMD_DONE;
}


#if 0
#include "lwip/sockets.h"

void testlwip()
{
	int Sock;
	fd_set fdsetR;
	FD_ZERO(&fdsetR);
	FD_SET(Sock, &fdsetR);
	fd_set fdsetE = fdsetR;

	int rc;
	const int cMillies = 10000;
	struct timeval timeout;
	timeout.tv_sec = cMillies / 1000;
	timeout.tv_usec = (cMillies % 1000) * 1000;
	//rc = lwip_select(Sock + 1, &fdsetR, NULL, &fdsetE, &timeout);
}
#endif
