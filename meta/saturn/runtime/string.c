/* Built with -nostdlib, and GCC still emits calls to these for things like
 * clearing a struct, so they have to exist. */

void* memset(void* s, int c, unsigned long n) {
    unsigned char* p = (unsigned char*)s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

void* memcpy(void* dest, const void* src, unsigned long n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--)
        *d++ = *s++;
    return dest;
}
