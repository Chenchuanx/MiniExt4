#include <linux/string.h>

int8_t strcmp(const int8_t * src, const int8_t * dest)
{
    while((*src != '\0') && (*src == *dest))
    {
        ++src;
        ++dest;
    }
    return *src - *dest;
}

int strlen(const char * s)
{
    int len = 0;
    while(s[len]) len++;
    return len;
}