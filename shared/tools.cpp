// implementation of generic tools

#include "cube.h"

////////////////////////// rnd numbers ////////////////////////////////////////

#define N              (624)             
#define M              (397)                
#define K              (0x9908B0DFU)       
#define hiBit(u)       ((u) & 0x80000000U)  
#define loBit(u)       ((u) & 0x00000001U)  
#define loBits(u)      ((u) & 0x7FFFFFFFU)  
#define mixBits(u, v)  (hiBit(u)|loBits(v)) 

static uint state[N+1];     
static uint *next;          
static int left = -1;     

void seedMT(uint seed)
{
    register uint x = (seed | 1U) & 0xFFFFFFFFU, *s = state;
    register int j;
    for(left=0, *s++=x, j=N; --j; *s++ = (x*=69069U) & 0xFFFFFFFFU);
}

uint reloadMT(void)
{
    register uint *p0=state, *p2=state+2, *pM=state+M, s0, s1;
    register int j;
    if(left < -1) seedMT(time(NULL));
    left=N-1, next=state+1;
    for(s0=state[0], s1=state[1], j=N-M+1; --j; s0=s1, s1=*p2++) *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    for(pM=state, j=M; --j; s0=s1, s1=*p2++) *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    s1=state[0], *p0 = *pM ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    s1 ^= (s1 >> 11);
    s1 ^= (s1 <<  7) & 0x9D2C5680U;
    s1 ^= (s1 << 15) & 0xEFC60000U;
    return(s1 ^ (s1 >> 18));
}

uint randomMT(void)
{
    uint y;
    if(--left < 0) return(reloadMT());
    y  = *next++;
    y ^= (y >> 11);
    y ^= (y <<  7) & 0x9D2C5680U;
    y ^= (y << 15) & 0xEFC60000U;
    return(y ^ (y >> 18));
}

// below is added by vampi

static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int base64_read_bit(char *s, int &bit) {
	int ofs = (bit >> 3);
	char *c = s + ofs;
	char *p;
	while(*c && !(p = strchr((char *)b64, *c))) { c++; bit += 8; }
	if(!*c) return -1;
	int v = p - b64;
	int mask = 0x20 >> (bit&0x07);
	int val = v & mask;
	bit++;
	if((bit & 0x07) >= 6) bit += 8 - (bit & 0x07);
	return val != 0;
}

int base64_read_byte(char *s, int &bit) {
	int val = 0;
	for(int i = 0; i < 8; i++) {
		int c = base64_read_bit(s, bit);
		if(c < 0) return -1;
		if(c) val |= (0x80 >> i);
	}
	return val;
}

bool base64_strcmp(const char *s, const char *s64) {
	const char *c = s; int b = 0;
	int bit = 0;
	while((b = base64_read_byte((char *)s64, bit)) >= 0) {
		if(*c != b) return false;
		c++;
	}
	if(!*c && b>=0) return false;
	if(b < 0 && *c) return false;
	return true;
}

void bufferevent_print_error(short what, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

#define checkerr(s) { if(what & BEV_EVENT_##s) printf(" %s", #s); }
	checkerr(CONNECTED);
	checkerr(READING);
	checkerr(WRITING);
	checkerr(EOF);
	checkerr(ERROR);
	checkerr(TIMEOUT);
	printf(" errno=%d \"%s\"\n", errno, strerror(errno));
}

void evdns_print_error(int result, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

#define DNSERR(x) if(result == DNS_ERR_##x) printf(" DNS_ERR_" #x);
	DNSERR(NONE); DNSERR(FORMAT); DNSERR(SERVERFAILED);
	DNSERR(NOTEXIST); DNSERR(NOTIMPL); DNSERR(REFUSED);
	DNSERR(TRUNCATED); DNSERR(UNKNOWN); DNSERR(TIMEOUT);
	DNSERR(SHUTDOWN); DNSERR(CANCEL);
	printf(" errno=%d \"%s\"\n", errno, strerror(errno));
}

void bufferevent_write_vprintf(struct bufferevent *be, const char *fmt, va_list ap) {
	struct evbuffer *eb = evbuffer_new();
	if(!eb) return;
	evbuffer_add_vprintf(eb, fmt, ap);
	bufferevent_write_buffer(be, eb);
	evbuffer_free(eb);
}

void bufferevent_write_printf(struct bufferevent *be, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	bufferevent_write_vprintf(be, fmt, ap);
	va_end(ap);
}

char *evbuffer_readln_nul(struct evbuffer *buffer, size_t *n_read_out, enum evbuffer_eol_style eol_style) {
	size_t len;
	char *result = evbuffer_readln(buffer, n_read_out, eol_style);
	if(result) return result;
	len = evbuffer_get_length(buffer);
	if(len == 0) return NULL;
	if(!(result = (char *)malloc(len+1))) return NULL;
	evbuffer_remove(buffer, result, len);
	result[len] = '\0';
	if(n_read_out) *n_read_out = len;
	return result;
}

#include "log-internal.h"
#include "mm-internal.h"

// Pavel Plesov's patch for libevent, not included in release yet
struct evhttp_uri *evhttp_uri_parse(const char *source_uri)
{
	char *readbuf = 0, *readp = 0, *token = 0, *query = 0, *host = 0, *port = 0;

	struct evhttp_uri *uri = (evhttp_uri *)calloc(1, sizeof(*uri));
	if (uri == NULL) {
		fprintf(stderr, "%s: calloc", __func__);
		return NULL;
	}

	readbuf = strdup(source_uri);
	if (readbuf == NULL) {
		fprintf(stderr, "%s: strdup", __func__);
		free(uri);
		return NULL;
	}

	readp = readbuf;
	token = NULL;

	/* 1. scheme:// */
	token = strstr(readp, "://");
	if (!token) {
		/* unsupported uri */
		free(readbuf);
		free(uri);
		return NULL;
	}

	*token = '\0';
	uri->scheme = strdup(readp);

	readp = token;
	readp += 3; /* eat :// */

	/* 2. query */
	query = strchr(readp, '/');
	if (query) {
		char *fragment = strchr(query, '#');
		if (fragment) {
			*fragment++ = '\0'; /* eat '#' */
			uri->fragment = strdup(fragment);
		}

		uri->query = strdup(query);
		*query = '\0'; /* eat '/' */
	}

	/* 3. user:pass@host:port */
	host = strchr(readp, '@');
	if (host) {
		char *pass = 0;
		/* got user:pass@host:port */
		*host++ = '\0'; /* eat @ */;
		pass = strchr(readp, ':');
		if (pass) {
			*pass++ = '\0'; /* eat ':' */
			uri->pass = strdup(pass);
		}

		uri->user = strdup(readp);
		readp = host;
	}

	/* 4. host:port */
	port = strchr(readp, ':');
	if (port) {
		*port++ = '\0'; /* eat ':' */
		uri->port = atoi(port);
	}

	/* 5. host */
	uri->host = strdup(readp);

	free(readbuf);

	return uri;
}

void evhttp_uri_free(struct evhttp_uri *uri)
{
	if (uri == NULL)
		return;

#define _URI_FREE_STR(f)\
	if (uri->f) {\
	free(uri->f);\
}

	_URI_FREE_STR(scheme);
	_URI_FREE_STR(user);
	_URI_FREE_STR(pass);
	_URI_FREE_STR(host);
	_URI_FREE_STR(query);
	_URI_FREE_STR(fragment);

	free(uri);

#undef _URI_FREE_STR
}

char *evhttp_uri_join(struct evhttp_uri *uri, void *buf, size_t limit)
{
	struct evbuffer *tmp = 0;
	unsigned char *joined = 0;
	size_t joined_size = 0;

#define _URI_ADD(f)evbuffer_add(tmp, uri->f, strlen(uri->f))
	if (!uri || !uri->scheme || !buf || !limit)
		return NULL;

	tmp = evbuffer_new();
	if (!tmp)
		return NULL;

	_URI_ADD(scheme);
	evbuffer_add(tmp, "://", 3);
	if (uri->host && *uri->host) {
		if (uri->user && *uri->user) {
			_URI_ADD(user);
			if (uri->pass && *uri->pass) {
				evbuffer_add(tmp, ":", 1);
				_URI_ADD(pass);
			}
			evbuffer_add(tmp, "@", 1);
		}

		_URI_ADD(host);

		if (uri->port > 0)
		evbuffer_add_printf(tmp,":%u", uri->port);
	}

	if (uri->query && *uri->query)
	_URI_ADD(query);

	if (uri->fragment && *uri->fragment) {
		if (!uri->query || !*uri->query)
		evbuffer_add(tmp, "/", 1);

		evbuffer_add(tmp, "#", 1);
		_URI_ADD(fragment);
	}

	evbuffer_add(tmp, "\0", 1); /* NUL */

	joined = evbuffer_pullup(tmp, -1);
	joined_size = evbuffer_get_length(tmp);

	if (joined_size < limit)
		memcpy(buf, joined, joined_size);
	else {
		memcpy(buf, joined, limit-1);
		*((char *)buf+ limit - 1) = '\0';
	}
	evbuffer_free(tmp);

	return (char *)buf;
#undef _URI_ADD
}
