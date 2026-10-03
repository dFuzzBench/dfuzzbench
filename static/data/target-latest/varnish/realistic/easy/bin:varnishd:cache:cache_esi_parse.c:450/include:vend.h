

#ifndef VEND_H_INCLUDED
#define VEND_H_INCLUDED

/* Alignment-agnostic encode/decode bytestream to/from little/big endian. */

static __inline uint16_t
vbe16dec(const void *pp)
{
	uint8_t const *p = (uint8_t const *)pp;

	return ((p[0] << 8) | p[1]);
}

static __inline uint32_t
vbe32dec(const void *pp)
{
	uint8_t const *p = (uint8_t const *)pp;

	return (((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static __inline uint64_t
vbe64dec(const void *pp)
{
	uint8_t const *p = (uint8_t const *)pp;

	return (((uint64_t)vbe32dec(p) << 32) | vbe32dec(p + 4));
}

#if 0
static __inline uint16_t
vle16dec(const void *pp)
{
	uint8_t const *p = (uint8_t const *)pp;

	return ((p[1] << 8) | p[0]);
}
#endif

static __inline uint32_t
vle32dec(const void *pp)
{
	uint8_t const *p = (uint8_t const *)pp;

	return (((unsigned)p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0]);
}

#if 0
static __inline uint64_t
vle64dec(const void *pp)
{
	uint8_t const *p = (uint8_t const *)pp;

	return (((uint64_t)vle32dec(p + 4) << 32) | vle32dec(p));
}
#endif

static __inline void
vbe16enc(void *pp, uint16_t u)
{
	uint8_t *p = (uint8_t *)pp;

	p[0] = (u >> 8) & 0xff;
	p[1] = u & 0xff;
}

static __inline void
vbe32enc(void *pp, uint32_t u)
{
	uint8_t *p = (uint8_t *)pp;

	p[0] = (u >> 24) & 0xff;
	p[1] = (u >> 16) & 0xff;
	p[2] = (u >> 8) & 0xff;
	p[3] = u & 0xff;
}

static __inline void
vbe64enc(void *pp, uint64_t u)
{
	uint8_t *p = (uint8_t *)pp;

	vbe32enc(p, (uint32_t)(u >> 32));
	vbe32enc(p + 4, (uint32_t)(u & 0xffffffffU));
}

static __inline void
vle16enc(void *pp, uint16_t u)
{
	uint8_t *p = (uint8_t *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
}

static __inline void
vle32enc(void *pp, uint32_t u)
{
	uint8_t *p = (uint8_t *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
	p[2] = (u >> 16) & 0xff;
	p[3] = (u >> 24) & 0xff;
}

#if 0
static __inline void
vle64enc(void *pp, uint64_t u)
{
	uint8_t *p = (uint8_t *)pp;

	vle32enc(p, (uint32_t)(u & 0xffffffffU));
	vle32enc(p + 4, (uint32_t)(u >> 32));
}
#endif

#endif
