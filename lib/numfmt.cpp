#include <lib/numfmt.h>

void u32_to_dec(char *buf, int buf_size, unsigned long v)
{
    if (buf_size <= 1) {
        return;
    }

    char tmp[16];
    int pos = 0;

    if (v == 0U) {
        tmp[pos++] = '0';
    } else {
        while (v > 0U && pos < (int)sizeof(tmp)) {
            unsigned int d = v % 10U;
            tmp[pos++] = (char)('0' + d);
            v /= 10U;
        }
    }

    int out = 0;
    if (pos >= buf_size) {
        pos = buf_size - 1;
    }
    while (pos > 0) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
}

void u64_to_dec(char *buf, int buf_size, uint64_t v)
{
	if (buf_size <= 1) {
		return;
	}

	static const uint64_t pow10[] = {
		10000000000000000000ULL,
		1000000000000000000ULL,
		100000000000000000ULL,
		10000000000000000ULL,
		1000000000000000ULL,
		100000000000000ULL,
		10000000000000ULL,
		1000000000000ULL,
		100000000000ULL,
		10000000000ULL,
		1000000000ULL,
		100000000ULL,
		10000000ULL,
		1000000ULL,
		100000ULL,
		10000ULL,
		1000ULL,
		100ULL,
		10ULL,
		1ULL
	};

	int out = 0;
	int started = 0;
	for (int i = 0; i < (int)(sizeof(pow10) / sizeof(pow10[0])); i++) {
		uint8_t d = 0;
		while (v >= pow10[i]) {
			v -= pow10[i];
			d++;
		}

		if (d != 0 || started || i == (int)(sizeof(pow10) / sizeof(pow10[0])) - 1) {
			if (out < buf_size - 1) {
				buf[out++] = (char)('0' + d);
			}
			started = 1;
		}
	}
	buf[out] = '\0';
}

void readable_size(unsigned long size, char *buf, int buf_size)
{
    const char *units[] = { "B", "K", "M", "G", "T" };
    int unit = 0;

    unsigned long whole = size;
    unsigned long rem = 0;

    while (whole >= 1024U && unit < 4) {
        rem = whole & 1023U;
        whole >>= 10;
        unit++;
    }

    if (buf_size <= 1) {
        return;
    }

    if (unit == 0) {
        char num[16];
        u32_to_dec(num, sizeof(num), whole);
        int i = 0, j = 0;
        while (num[i] != '\0' && j < buf_size - 2) {
            buf[j++] = num[i++];
        }
        if (j < buf_size - 1) {
            buf[j++] = units[unit][0];
            buf[j] = '\0';
        } else {
            buf[buf_size - 1] = '\0';
        }
        return;
    }

    unsigned long decimal = (rem * 10U + 512U) >> 10;
    if (decimal >= 10U) {
        whole += 1U;
        decimal = 0U;
    }

    int j = 0;
    char num[16];
    u32_to_dec(num, sizeof(num), whole);
    for (int i = 0; num[i] != '\0' && j < buf_size - 1; i++) {
        buf[j++] = num[i];
    }

    if (decimal != 0U && j < buf_size - 2) {
        buf[j++] = '.';
        buf[j++] = (char)('0' + (int)decimal);
    }

    if (j < buf_size - 1) {
        buf[j++] = units[unit][0];
    }
    if (j >= buf_size) {
        j = buf_size - 1;
    }
    buf[j] = '\0';
}
