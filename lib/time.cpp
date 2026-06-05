#include <lib/time.h>

static void u32_to_dec_local(char *buf, int buf_size, unsigned long v)
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

// unix时间戳转换为年月日时分秒
static void break_unix_time(unsigned long secs, unsigned long *year, unsigned long *mon,
			    unsigned long *day, unsigned long *hour, unsigned long *min,
			    unsigned long *sec, unsigned long *wday)
{
	unsigned long days = secs / 86400UL;
	unsigned long rem = secs % 86400UL;
	int month_lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int leap;
	int month;

	*hour = rem / 3600UL;
	rem %= 3600UL;
	*min = rem / 60UL;
	*sec = rem % 60UL;
	*wday = (days + 4UL) % 7UL;

	*year = 1970;
	for (;;) {
		leap = ((*year % 4UL == 0 && *year % 100UL != 0) ||
			(*year % 400UL == 0)) ? 1 : 0;
		unsigned long ydays = (unsigned long)(leap ? 366 : 365);
		if (days >= ydays) {
			days -= ydays;
			(*year)++;
		} else {
			break;
		}
	}

	leap = ((*year % 4UL == 0 && *year % 100UL != 0) ||
		(*year % 400UL == 0)) ? 1 : 0;
	if (leap) {
		month_lengths[1] = 29;
	}

	month = 0;
	while (month < 12 && days >= (unsigned long)month_lengths[month]) {
		days -= (unsigned long)month_lengths[month];
		month++;
	}

	*day = days + 1;
	*mon = (unsigned long)(month + 1);
}

void format_time(unsigned long secs, char *buf, int buf_size)
{
	unsigned long year, mon, day, hour, min, sec, wday;
	int pos = 0;
	char tmp[16];

	(void)wday;
	if (buf_size < 20) {
		if (buf_size > 0) {
			buf[0] = '\0';
		}
		return;
	}

	break_unix_time(secs, &year, &mon, &day, &hour, &min, &sec, &wday);

	u32_to_dec_local(tmp, sizeof(tmp), year);
	{
		int i = 0;
		while (tmp[i] && pos < buf_size - 1) {
			buf[pos++] = tmp[i++];
		}
	}
	buf[pos++] = '-';

	if (mon < 10UL) {
		buf[pos++] = '0';
		buf[pos++] = (char)('0' + (int)mon);
	} else {
		u32_to_dec_local(tmp, sizeof(tmp), mon);
		buf[pos++] = tmp[0];
		buf[pos++] = tmp[1];
	}
	buf[pos++] = '-';

	if (day < 10UL) {
		buf[pos++] = '0';
		buf[pos++] = (char)('0' + (int)day);
	} else {
		u32_to_dec_local(tmp, sizeof(tmp), day);
		buf[pos++] = tmp[0];
		buf[pos++] = tmp[1];
	}

	buf[pos++] = ' ';

	if (hour < 10UL) {
		buf[pos++] = '0';
		buf[pos++] = (char)('0' + (int)hour);
	} else {
		u32_to_dec_local(tmp, sizeof(tmp), hour);
		buf[pos++] = tmp[0];
		buf[pos++] = tmp[1];
	}
	buf[pos++] = ':';

	if (min < 10UL) {
		buf[pos++] = '0';
		buf[pos++] = (char)('0' + (int)min);
	} else {
		u32_to_dec_local(tmp, sizeof(tmp), min);
		buf[pos++] = tmp[0];
		buf[pos++] = tmp[1];
	}
	buf[pos++] = ':';

	if (sec < 10UL) {
		buf[pos++] = '0';
		buf[pos++] = (char)('0' + (int)sec);
	} else {
		u32_to_dec_local(tmp, sizeof(tmp), sec);
		buf[pos++] = tmp[0];
		buf[pos++] = tmp[1];
	}

	if (pos >= buf_size) {
		pos = buf_size - 1;
	}
	buf[pos] = '\0';
}

void format_date_default(unsigned long secs, char *buf, int buf_size)
{
	static const char *const wdays[7] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char *const mons[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	unsigned long year, mon, day, hour, min, sec, wday;
	int pos = 0;
	const char *w;
	const char *m;
	int i;

	if (buf_size < 32) {
		if (buf_size > 0) {
			buf[0] = '\0';
		}
		return;
	}

	break_unix_time(secs, &year, &mon, &day, &hour, &min, &sec, &wday);
	w = (wday < 7) ? wdays[wday] : "???";
	m = (mon >= 1 && mon <= 12) ? mons[mon - 1] : "???";

	for (i = 0; w[i] && pos < buf_size - 1; i++) {
		buf[pos++] = w[i];
	}
	buf[pos++] = ' ';
	for (i = 0; m[i] && pos < buf_size - 1; i++) {
		buf[pos++] = m[i];
	}
	buf[pos++] = ' ';
	if (day < 10UL) {
		buf[pos++] = ' ';
		buf[pos++] = (char)('0' + (int)day);
	} else {
		buf[pos++] = (char)('0' + (int)(day / 10UL));
		buf[pos++] = (char)('0' + (int)(day % 10UL));
	}
	buf[pos++] = ' ';
	buf[pos++] = (char)('0' + (int)(hour / 10UL));
	buf[pos++] = (char)('0' + (int)(hour % 10UL));
	buf[pos++] = ':';
	buf[pos++] = (char)('0' + (int)(min / 10UL));
	buf[pos++] = (char)('0' + (int)(min % 10UL));
	buf[pos++] = ':';
	buf[pos++] = (char)('0' + (int)(sec / 10UL));
	buf[pos++] = (char)('0' + (int)(sec % 10UL));
	buf[pos++] = ' ';
	for (i = 0; i < 3 && pos < buf_size - 1; i++) {
		buf[pos++] = "UTC"[i];
	}
	buf[pos++] = ' ';
	u32_to_dec_local(buf + pos, buf_size - pos, year);
}
