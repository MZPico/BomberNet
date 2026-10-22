/* memcpy and memset of our own (docs/port-amiga.md): libnix's memcpy is
 * exec's CopyMem, and the AROS ROM's CopyMem faults on a 68000 when the
 * source and destination are not aligned alike (an address error), which
 * the game's byte buffers (network bytes at odd offsets into aligned
 * arrays) bring about every frame. Bytes when the alignments differ,
 * words when they agree. */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = dst;
  const uint8_t *s = src;
  if (n >= 4 && (((uintptr_t)d ^ (uintptr_t)s) & 1) == 0) {
    if ((uintptr_t)d & 1) { *d++ = *s++; n--; }
    for (; n >= 2; n -= 2) { *(uint16_t *)d = *(const uint16_t *)s; d += 2; s += 2; }
  }
  while (n--) *d++ = *s++;
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  uint8_t *d = dst;
  if (n >= 4) {
    uint16_t w = (uint16_t)((uint8_t)c << 8 | (uint8_t)c);
    if ((uintptr_t)d & 1) { *d++ = (uint8_t)c; n--; }
    for (; n >= 2; n -= 2) { *(uint16_t *)d = w; d += 2; }
  }
  while (n--) *d++ = (uint8_t)c;
  return dst;
}
