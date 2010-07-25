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
