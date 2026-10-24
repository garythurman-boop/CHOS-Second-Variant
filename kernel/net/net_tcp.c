#include <stddef.h>
#include <stdint.h>
#include "chos/net_tcp.h"

#define TCP_INITIAL_RTO_TICKS 100u
#define TCP_MAX_RTO_TICKS 800u
#define TCP_MAX_RETRIES 5u
#define TCP_UNSENT_TIMEOUT_TICKS 1000u
#define TCP_TIME_WAIT_TICKS 200u
#define TCP_EPHEMERAL_FIRST 49152u
#define TCP_EPHEMERAL_COUNT 16384u

#define TCP_FLAG_SET(v, f) (((v) & (f)) != 0u)

typedef struct {
    int allocated;
    uint32_t owner_pid;
    chos_tcp_state_t state;
    chos_tcp_error_t error;
    uint16_t local_port, remote_port;
    uint32_t remote_ipv4;
    uint32_t snd_una, snd_nxt, rcv_nxt;
    uint32_t remote_window;
    uint32_t retransmissions;
    uint64_t bytes_sent, bytes_received;

    int pending_valid;
    int pending_sent;
    uint8_t pending_flags;
    uint32_t pending_sequence;
    uint32_t pending_end;
    uint64_t pending_since;
    uint64_t retransmit_deadline;
    uint32_t rto_ticks;
    uint8_t pending_payload[CHOS_TCP_MSS];
    size_t pending_length;

    int ack_pending;
    uint64_t ack_since;
    uint64_t time_wait_deadline;
    uint64_t fin_wait_2_deadline;

    uint8_t receive_buffer[CHOS_TCP_RX_CAPACITY];
    size_t receive_head;
    size_t receive_count;
} tcp_socket_t;

static tcp_socket_t sockets[CHOS_TCP_MAX_SOCKETS];
static uint32_t local_address;
static uint16_t next_ephemeral = TCP_EPHEMERAL_FIRST;
static chos_tcp_emit_fn send_callback;
static chos_tcp_clock_fn clock_callback;

static uint64_t tcp_now(void) { return clock_callback != NULL ? clock_callback() : 0u; }

static void zero_bytes(void *ptr, size_t length) {
    uint8_t *p = (uint8_t *)ptr;
    size_t i;
    for (i = 0u; i < length; ++i) p[i] = 0u;
}

static void copy_bytes(void *dst, const void *src, size_t length) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0u; i < length; ++i) d[i] = s[i];
}

static int seq_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }
static int seq_le(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

static tcp_socket_t *socket_for(int handle) {
    if (handle < 1 || handle > (int)CHOS_TCP_MAX_SOCKETS) return NULL;
    if (!sockets[(unsigned)(handle - 1)].allocated) return NULL;
    return &sockets[(unsigned)(handle - 1)];
}

static tcp_socket_t *socket_for_owner(uint32_t owner_pid, int handle) {
    tcp_socket_t *s = socket_for(handle);
    if (s == NULL || s->owner_pid != owner_pid) return NULL;
    return s;
}

static void clear_pending(tcp_socket_t *s) {
    s->pending_valid = 0;
    s->pending_sent = 0;
    s->pending_flags = 0u;
    s->pending_sequence = 0u;
    s->pending_end = 0u;
    s->pending_since = 0u;
    s->retransmit_deadline = 0u;
    s->pending_length = 0u;
    s->rto_ticks = TCP_INITIAL_RTO_TICKS;
    s->retransmissions = 0u;
}

static void fail_socket(tcp_socket_t *s, chos_tcp_error_t error) {
    clear_pending(s);
    s->ack_pending = 0;
    s->state = CHOS_TCP_FAILED;
    s->error = error;
}

static int emit_segment(tcp_socket_t *s, uint32_t seq, uint8_t flags,
                        const void *payload, size_t payload_length) {
    uint16_t window;
    size_t available = CHOS_TCP_RX_CAPACITY - s->receive_count;
    int rc;
    if (send_callback == NULL || local_address == 0u || s->remote_ipv4 == 0u) return -1;
    window = available > 0xffffu ? 0xffffu : (uint16_t)available;
    rc = send_callback(local_address, s->remote_ipv4, s->local_port, s->remote_port,
                       seq, s->rcv_nxt, flags, window, payload, payload_length);
    return rc;
}

static int queue_segment(tcp_socket_t *s, uint8_t flags, const void *payload, size_t length) {
    uint32_t consumed;
    if (s == NULL || s->pending_valid || length > CHOS_TCP_MSS ||
        (length != 0u && payload == NULL)) return 0;
    s->pending_sequence = s->snd_nxt;
    s->pending_flags = flags;
    s->pending_length = length;
    if (length != 0u) copy_bytes(s->pending_payload, payload, length);
    consumed = (uint32_t)length;
    if (TCP_FLAG_SET(flags, CHOS_TCP_FLAG_SYN)) ++consumed;
    if (TCP_FLAG_SET(flags, CHOS_TCP_FLAG_FIN)) ++consumed;
    s->pending_end = s->pending_sequence + consumed;
    s->snd_nxt = s->pending_end;
    s->pending_valid = 1;
    s->pending_sent = 0;
    s->pending_since = tcp_now();
    s->rto_ticks = TCP_INITIAL_RTO_TICKS;
    s->retransmissions = 0u;
    return 1;
}

static void request_ack(tcp_socket_t *s) {
    int rc;
    if (s == NULL) return;
    rc = emit_segment(s, s->snd_nxt, CHOS_TCP_FLAG_ACK, NULL, 0u);
    if (rc > 0) {
        s->ack_pending = 0;
    } else if (rc == 0) {
        if (!s->ack_pending) s->ack_since = tcp_now();
        s->ack_pending = 1;
    } else {
        fail_socket(s, CHOS_TCP_ERROR_NETWORK);
    }
}

static int port_in_use(uint16_t port) {
    size_t i;
    for (i = 0u; i < CHOS_TCP_MAX_SOCKETS; ++i) {
        if (sockets[i].allocated && sockets[i].local_port == port &&
            sockets[i].state != CHOS_TCP_CLOSED && sockets[i].state != CHOS_TCP_FAILED)
            return 1;
    }
    return 0;
}

static uint16_t choose_ephemeral_port(void) {
    uint32_t attempts;
    for (attempts = 0u; attempts < TCP_EPHEMERAL_COUNT; ++attempts) {
        uint16_t candidate = next_ephemeral;
        ++next_ephemeral;
        if (next_ephemeral < TCP_EPHEMERAL_FIRST) next_ephemeral = TCP_EPHEMERAL_FIRST;
        if (!port_in_use(candidate)) return candidate;
    }
    return 0u;
}

void chos_tcp_init(chos_tcp_emit_fn emit, chos_tcp_clock_fn clock_fn) {
    size_t i;
    zero_bytes(sockets, sizeof(sockets));
    local_address = 0u;
    next_ephemeral = TCP_EPHEMERAL_FIRST;
    send_callback = emit;
    clock_callback = clock_fn;
    for (i = 0u; i < CHOS_TCP_MAX_SOCKETS; ++i) sockets[i].rto_ticks = TCP_INITIAL_RTO_TICKS;
}

void chos_tcp_set_local_address(uint32_t ip) {
    size_t i;
    uint32_t previous = local_address;
    if (previous == ip) return;
    local_address = ip;
    /* A lease change invalidates every established or pending tuple. */
    if (previous != 0u) {
        for (i = 0u; i < CHOS_TCP_MAX_SOCKETS; ++i) {
            if (sockets[i].allocated && sockets[i].state != CHOS_TCP_CLOSED &&
                sockets[i].state != CHOS_TCP_FAILED) fail_socket(&sockets[i], CHOS_TCP_ERROR_NETWORK);
        }
    }
}

int chos_tcp_open_for_owner(uint32_t owner_pid, uint16_t local_port) {
    size_t i;
    if (send_callback == NULL) return -1;
    if (local_port == 0u) {
        local_port = choose_ephemeral_port();
        if (local_port == 0u) return -1;
    } else if (port_in_use(local_port)) return -1;
    for (i = 0u; i < CHOS_TCP_MAX_SOCKETS; ++i) {
        tcp_socket_t *s = &sockets[i];
        /* An allocated handle remains reserved to its owner until explicit close
         * or process-exit cleanup, even while its TCP state is CLOSED/FAILED. */
        if (!s->allocated) {
            zero_bytes(s, sizeof(*s));
            s->allocated = 1;
            s->owner_pid = owner_pid;
            s->state = CHOS_TCP_CLOSED;
            s->local_port = local_port;
            s->rto_ticks = TCP_INITIAL_RTO_TICKS;
            return (int)i + 1;
        }
    }
    return -1;
}

int chos_tcp_open(uint16_t local_port) {
    return chos_tcp_open_for_owner(0u, local_port);
}

int chos_tcp_connect_for_owner(uint32_t owner_pid, int handle, uint32_t remote_ip, uint16_t remote_port) {
    tcp_socket_t *s = socket_for_owner(owner_pid, handle);
    uint32_t seed;
    if (s == NULL || s->state != CHOS_TCP_CLOSED || local_address == 0u ||
        remote_ip == 0u || remote_ip == 0xffffffffu || remote_port == 0u ||
        remote_port == s->local_port) return 0;
    s->remote_ipv4 = remote_ip;
    s->remote_port = remote_port;
    seed = (uint32_t)tcp_now() ^ local_address ^ (remote_ip * 0x9e3779b9u) ^
           ((uint32_t)s->local_port << 16) ^ remote_port ^ (uint32_t)handle * 0x43484f53u;
    if (seed == 0u) seed = 0x43484f53u;
    s->snd_una = seed;
    s->snd_nxt = seed;
    s->rcv_nxt = 0u;
    s->remote_window = 0xffffu;
    s->state = CHOS_TCP_SYN_SENT;
    s->error = CHOS_TCP_ERROR_NONE;
    if (!queue_segment(s, CHOS_TCP_FLAG_SYN, NULL, 0u)) {
        fail_socket(s, CHOS_TCP_ERROR_NETWORK);
        return 0;
    }
    return 1;
}

int chos_tcp_connect(int handle, uint32_t remote_ip, uint16_t remote_port) {
    return chos_tcp_connect_for_owner(0u, handle, remote_ip, remote_port);
}

int chos_tcp_send_for_owner(uint32_t owner_pid, int handle, const void *data, size_t length) {
    tcp_socket_t *s = socket_for_owner(owner_pid, handle);
    size_t amount;
    if (s == NULL) return -1;
    if (length == 0u) return 0;
    if (data == NULL) return -1;
    /* TCP is still writable in CLOSE-WAIT: the peer has closed its sending
     * direction, but our sending direction remains open until we send FIN. */
    if ((s->state != CHOS_TCP_ESTABLISHED && s->state != CHOS_TCP_CLOSE_WAIT) ||
        s->pending_valid || s->remote_window == 0u) return 0;
    amount = length;
    if (amount > CHOS_TCP_MSS) amount = CHOS_TCP_MSS;
    if (amount > s->remote_window) amount = s->remote_window;
    if (amount == 0u) return 0;
    if (!queue_segment(s, CHOS_TCP_FLAG_ACK | CHOS_TCP_FLAG_PSH, data, amount)) return 0;
    s->bytes_sent += amount;
    return (int)amount;
}

int chos_tcp_send(int handle, const void *data, size_t length) {
    return chos_tcp_send_for_owner(0u, handle, data, length);
}

int chos_tcp_recv_for_owner(uint32_t owner_pid, int handle, void *buffer, size_t capacity) {
    tcp_socket_t *s = socket_for_owner(owner_pid, handle);
    uint8_t *out = (uint8_t *)buffer;
    size_t amount, i;
    if (s == NULL || (capacity != 0u && buffer == NULL)) return -1;
    amount = s->receive_count;
    if (amount > capacity) amount = capacity;
    for (i = 0u; i < amount; ++i) {
        out[i] = s->receive_buffer[s->receive_head];
        s->receive_head = (s->receive_head + 1u) % CHOS_TCP_RX_CAPACITY;
    }
    s->receive_count -= amount;
    if (amount != 0u) request_ack(s); /* advertise the newly freed receive window */
    return (int)amount;
}

int chos_tcp_recv(int handle, void *buffer, size_t capacity) {
    return chos_tcp_recv_for_owner(0u, handle, buffer, capacity);
}

int chos_tcp_close_for_owner(uint32_t owner_pid, int handle) {
    tcp_socket_t *s = socket_for_owner(owner_pid, handle);
    if (s == NULL) return -1;
    if (s->state == CHOS_TCP_CLOSED || s->state == CHOS_TCP_FAILED) {
        zero_bytes(s, sizeof(*s));
        return 1;
    }
    if (s->state == CHOS_TCP_SYN_SENT) {
        zero_bytes(s, sizeof(*s));
        return 1;
    }
    if (s->pending_valid) return 0;
    if (s->state == CHOS_TCP_ESTABLISHED) {
        if (!queue_segment(s, CHOS_TCP_FLAG_FIN | CHOS_TCP_FLAG_ACK, NULL, 0u)) return 0;
        s->state = CHOS_TCP_FIN_WAIT_1;
        return 1;
    }
    if (s->state == CHOS_TCP_CLOSE_WAIT) {
        if (!queue_segment(s, CHOS_TCP_FLAG_FIN | CHOS_TCP_FLAG_ACK, NULL, 0u)) return 0;
        s->state = CHOS_TCP_LAST_ACK;
        return 1;
    }
    return 0;
}

int chos_tcp_close(int handle) {
    return chos_tcp_close_for_owner(0u, handle);
}

int chos_tcp_abort_for_owner(uint32_t owner_pid, int handle) {
    tcp_socket_t *s = socket_for_owner(owner_pid, handle);
    if (s == NULL) return -1;
    if (s->state != CHOS_TCP_CLOSED && s->state != CHOS_TCP_FAILED &&
        s->remote_ipv4 != 0u && s->remote_port != 0u && local_address != 0u) {
        uint8_t flags = CHOS_TCP_FLAG_RST;
        if (s->rcv_nxt != 0u) flags |= CHOS_TCP_FLAG_ACK;
        /* Best-effort RST: resource release must not depend on ARP/TX readiness. */
        (void)emit_segment(s, s->snd_nxt, flags, NULL, 0u);
    }
    zero_bytes(s, sizeof(*s));
    return 1;
}

int chos_tcp_get_info_for_owner(uint32_t owner_pid, int handle, chos_tcp_socket_info_t *out) {
    tcp_socket_t *s = socket_for_owner(owner_pid, handle);
    if (s == NULL || out == NULL) return 0;
    out->handle = handle;
    out->state = s->state;
    out->error = s->error;
    out->local_ipv4 = local_address;
    out->remote_ipv4 = s->remote_ipv4;
    out->local_port = s->local_port;
    out->remote_port = s->remote_port;
    out->send_unacknowledged = s->snd_una;
    out->send_next = s->snd_nxt;
    out->receive_next = s->rcv_nxt;
    out->retransmissions = s->retransmissions;
    out->remote_window = s->remote_window;
    out->receive_available = s->receive_count;
    out->bytes_sent = s->bytes_sent;
    out->bytes_received = s->bytes_received;
    return 1;
}

int chos_tcp_get_info(int handle, chos_tcp_socket_info_t *out) {
    return chos_tcp_get_info_for_owner(0u, handle, out);
}

void chos_tcp_drop_owner(uint32_t owner_pid) {
    size_t i;
    if (owner_pid == 0u) return;
    for (i = 0u; i < CHOS_TCP_MAX_SOCKETS; ++i) {
        tcp_socket_t *s = &sockets[i];
        if (s->allocated && s->owner_pid == owner_pid) zero_bytes(s, sizeof(*s));
    }
}

static void acknowledge_outstanding(tcp_socket_t *s, uint32_t ack) {
    if (!seq_ge(ack, s->snd_una) || !seq_le(ack, s->snd_nxt)) return;
    s->snd_una = ack;
    if (s->pending_valid && s->pending_sent && seq_ge(ack, s->pending_end)) {
        uint8_t flags = s->pending_flags;
        clear_pending(s);
        if ((flags & CHOS_TCP_FLAG_FIN) != 0u) {
            if (s->state == CHOS_TCP_FIN_WAIT_1) {
                s->state = CHOS_TCP_FIN_WAIT_2;
                s->fin_wait_2_deadline = tcp_now() + TCP_TIME_WAIT_TICKS;
            } else if (s->state == CHOS_TCP_LAST_ACK) s->state = CHOS_TCP_CLOSED;
        }
    }
}

static void append_rx(tcp_socket_t *s, const uint8_t *data, size_t length) {
    size_t room = CHOS_TCP_RX_CAPACITY - s->receive_count;
    size_t amount = length < room ? length : room;
    size_t i, tail;
    if (amount == 0u) return;
    tail = (s->receive_head + s->receive_count) % CHOS_TCP_RX_CAPACITY;
    for (i = 0u; i < amount; ++i) {
        s->receive_buffer[tail] = data[i];
        tail = (tail + 1u) % CHOS_TCP_RX_CAPACITY;
    }
    s->receive_count += amount;
    s->bytes_received += amount;
}

static void handle_data_and_fin_at_sequence(tcp_socket_t *s, const chos_tcp_view_t *tcp, uint32_t seq) {
    size_t accepted = 0u;
    int in_order = (seq == s->rcv_nxt);
    if (tcp->payload_length != 0u && tcp->payload != NULL && in_order) {
        size_t room = CHOS_TCP_RX_CAPACITY - s->receive_count;
        accepted = tcp->payload_length < room ? tcp->payload_length : room;
        append_rx(s, tcp->payload, accepted);
        s->rcv_nxt += (uint32_t)accepted;
    }
    if (TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_FIN) && in_order &&
        accepted == tcp->payload_length && seq + (uint32_t)tcp->payload_length == s->rcv_nxt) {
        ++s->rcv_nxt;
        if (s->state == CHOS_TCP_ESTABLISHED) s->state = CHOS_TCP_CLOSE_WAIT;
        else if (s->state == CHOS_TCP_FIN_WAIT_2) {
            s->state = CHOS_TCP_TIME_WAIT;
            s->time_wait_deadline = tcp_now() + TCP_TIME_WAIT_TICKS;
        } else if (s->state == CHOS_TCP_FIN_WAIT_1) {
            s->state = CHOS_TCP_TIME_WAIT;
            s->time_wait_deadline = tcp_now() + TCP_TIME_WAIT_TICKS;
        }
    }
    if (tcp->payload_length != 0u || TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_FIN) || !in_order)
        request_ack(s);
}

void chos_tcp_input(const chos_ipv4_view_t *ip, const chos_tcp_view_t *tcp) {
    size_t i;
    tcp_socket_t *s = NULL;
    if (ip == NULL || tcp == NULL || ip->protocol != 6u || ip->destination_address != local_address) return;
    for (i = 0u; i < CHOS_TCP_MAX_SOCKETS; ++i) {
        tcp_socket_t *candidate = &sockets[i];
        if (candidate->allocated && candidate->state != CHOS_TCP_CLOSED &&
            candidate->state != CHOS_TCP_FAILED && candidate->remote_ipv4 == ip->source_address &&
            candidate->local_port == tcp->destination_port && candidate->remote_port == tcp->source_port) {
            s = candidate; break;
        }
    }
    if (s == NULL) return;
    s->remote_window = tcp->window;
    if (TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_RST)) {
        fail_socket(s, CHOS_TCP_ERROR_RESET);
        return;
    }
    if (s->state == CHOS_TCP_SYN_SENT) {
        if (TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_SYN) && TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_ACK) &&
            tcp->acknowledgement == s->snd_nxt && s->pending_valid && s->pending_sent &&
            TCP_FLAG_SET(s->pending_flags, CHOS_TCP_FLAG_SYN)) {
            s->snd_una = tcp->acknowledgement;
            clear_pending(s);
            s->rcv_nxt = tcp->sequence + 1u;
            s->state = CHOS_TCP_ESTABLISHED;
            s->error = CHOS_TCP_ERROR_NONE;
            request_ack(s);
            if (tcp->payload_length != 0u || TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_FIN)) {
                handle_data_and_fin_at_sequence(s, tcp, tcp->sequence + 1u);
            }
        }
        return;
    }
    if (TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_ACK)) {
        acknowledge_outstanding(s, tcp->acknowledgement);
        if (s->state == CHOS_TCP_CLOSED) return;
    }
    /* Retransmitted SYN+ACK after a lost handshake ACK: repeat the ACK. */
    if (TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_SYN) && TCP_FLAG_SET(tcp->flags, CHOS_TCP_FLAG_ACK) &&
        tcp->sequence + 1u == s->rcv_nxt && tcp->acknowledgement == s->snd_nxt) {
        request_ack(s);
        return;
    }
    if (s->state == CHOS_TCP_ESTABLISHED || s->state == CHOS_TCP_CLOSE_WAIT ||
        s->state == CHOS_TCP_FIN_WAIT_1 || s->state == CHOS_TCP_FIN_WAIT_2) {
        handle_data_and_fin_at_sequence(s, tcp, tcp->sequence);
    }
}

static void service_ack(tcp_socket_t *s, uint64_t now) {
    int rc;
    if (!s->ack_pending) return;
    if (now - s->ack_since >= TCP_UNSENT_TIMEOUT_TICKS) {
        s->ack_pending = 0;
        return;
    }
    rc = emit_segment(s, s->snd_nxt, CHOS_TCP_FLAG_ACK, NULL, 0u);
    if (rc > 0) s->ack_pending = 0;
    else if (rc < 0) fail_socket(s, CHOS_TCP_ERROR_NETWORK);
}

static void service_pending(tcp_socket_t *s, uint64_t now) {
    int rc;
    if (!s->pending_valid) return;
    if (s->pending_sent && now >= s->retransmit_deadline) {
        if (s->retransmissions >= TCP_MAX_RETRIES) {
            fail_socket(s, CHOS_TCP_ERROR_TIMEOUT);
            return;
        }
        ++s->retransmissions;
        s->pending_sent = 0;
        s->pending_since = now;
        if (s->rto_ticks < TCP_MAX_RTO_TICKS) {
            s->rto_ticks *= 2u;
            if (s->rto_ticks > TCP_MAX_RTO_TICKS) s->rto_ticks = TCP_MAX_RTO_TICKS;
        }
    }
    if (!s->pending_sent) {
        if (now - s->pending_since >= TCP_UNSENT_TIMEOUT_TICKS) {
            fail_socket(s, CHOS_TCP_ERROR_TIMEOUT);
            return;
        }
        rc = emit_segment(s, s->pending_sequence, s->pending_flags,
                          s->pending_payload, s->pending_length);
        if (rc > 0) {
            s->pending_sent = 1;
            s->retransmit_deadline = now + s->rto_ticks;
        } else if (rc < 0) {
            fail_socket(s, CHOS_TCP_ERROR_NETWORK);
        }
    }
}

void chos_tcp_poll(void) {
    size_t i;
    uint64_t now = tcp_now();
    for (i = 0u; i < CHOS_TCP_MAX_SOCKETS; ++i) {
        tcp_socket_t *s = &sockets[i];
        if (!s->allocated) continue;
        if (s->state == CHOS_TCP_TIME_WAIT && now >= s->time_wait_deadline) {
            s->state = CHOS_TCP_CLOSED;
            continue;
        }
        /* A peer may ACK our FIN but keep its sending direction open forever.
         * Bound FIN-WAIT-2 so a client socket and its ephemeral port cannot leak. */
        if (s->state == CHOS_TCP_FIN_WAIT_2 && s->fin_wait_2_deadline != 0u &&
            now >= s->fin_wait_2_deadline) {
            s->state = CHOS_TCP_CLOSED;
            continue;
        }
        if (s->state == CHOS_TCP_FAILED || s->state == CHOS_TCP_CLOSED) continue;
        service_ack(s, now);
        service_pending(s, now);
    }
}

const char *chos_tcp_state_name(chos_tcp_state_t state) {
    switch (state) {
        case CHOS_TCP_CLOSED: return "closed";
        case CHOS_TCP_SYN_SENT: return "syn-sent";
        case CHOS_TCP_ESTABLISHED: return "established";
        case CHOS_TCP_FIN_WAIT_1: return "fin-wait-1";
        case CHOS_TCP_FIN_WAIT_2: return "fin-wait-2";
        case CHOS_TCP_CLOSE_WAIT: return "close-wait";
        case CHOS_TCP_LAST_ACK: return "last-ack";
        case CHOS_TCP_TIME_WAIT: return "time-wait";
        case CHOS_TCP_FAILED: return "failed";
        default: return "unknown";
    }
}

const char *chos_tcp_error_name(chos_tcp_error_t error) {
    switch (error) {
        case CHOS_TCP_ERROR_NONE: return "none";
        case CHOS_TCP_ERROR_TIMEOUT: return "timeout";
        case CHOS_TCP_ERROR_RESET: return "reset";
        case CHOS_TCP_ERROR_NETWORK: return "network";
        case CHOS_TCP_ERROR_ARGUMENT: return "argument";
        default: return "unknown";
    }
}
