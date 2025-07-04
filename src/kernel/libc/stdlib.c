#include <kernel/libc/stdlib.h>

void itoa(uintptr_t num, char *str, int base) {
  int i = 0;
  int is_negative = 0;

  /*  Handle 0 explicitly */
  if (num == 0) {
    str[i++] = '0';
    str[i] = '\0';
    return;
  }

  /*  Process digits */
  while (num != 0) {
    int rem = num % base;
    str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
    num /= base;
  }

  /*  Add negative sign if applicable */
  if (is_negative) {
    str[i++] = '-';
  }

  str[i] = '\0'; /*  Null-terminate the string */

  /*  Reverse the string */
  int start = 0, end = i - 1;
  while (start < end) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
    start++;
    end--;
  }
}
