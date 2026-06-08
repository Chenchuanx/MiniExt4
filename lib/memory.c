#include <linux/memory.h>

void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	size_t i;

	for (i = 0; i < n; i++) {
		p[i] = (unsigned char)c;
	}
	return s;
}

void *simple_memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	size_t i;

	for (i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dest;
}

int simple_memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	size_t i;

	for (i = 0; i < n; i++) {
		if (p1[i] != p2[i]) {
			return p1[i] - p2[i];
		}
	}
	return 0;
}

void *simple_memmove(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	size_t i;

	if (!dest || !src || n == 0) {
		return dest;
	}
	if (d < s) {
		for (i = 0; i < n; i++) {
			d[i] = s[i];
		}
	} else {
		for (i = n; i > 0; i--) {
			d[i - 1] = s[i - 1];
		}
	}
	return dest;
}
