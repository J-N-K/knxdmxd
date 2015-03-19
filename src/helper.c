/*
 * helper.c
 *
 * Created on: 2015-01-31
 *      
 * Author: Jan N. Klug
 * 
 */

#include <helper.h>

/*
 * str2knx - convert string to KNX address
 */

eibaddr_t str2knx(const char *gastr) {
  unsigned int a, b, c;
  if (sscanf(gastr, "%u/%u/%u", &a, &b, &c) == 3)
    return ((a & 0x01f) << 11) | ((b & 0x07) << 8) | ((c & 0xff));
  if (sscanf(gastr, "%u/%u", &a, &b) == 2)
    return ((a & 0x01f) << 11) | ((b & 0x7FF));
  if (sscanf(gastr, "%x", &a) == 1)
    return a & 0xffff;
  syslog(LOG_WARNING, "str2: invalid group address %s", gastr);
  return 0;
}

/*
 * strdcopy - make duplicate of string and return success 
 */

bool strdcopy(char **dst, const char *src) {
  int len = strlen(src) + 1;
  // syslog(LOG_DEBUG, "strdcopy: allocating %d bytes for %s", len, src);
  *dst = (char*) malloc(len);
  if (*dst == NULL) {
    return false;
  } else {
    strncpy(*dst, src, len);
    return true;
  }
}

/*
 * str2dmx - convert string to DMX address
 */

void str2dmx(dmxaddr_t *dmx, const char *dmxstr) {
  sscanf(dmxstr, "%u.%d", &dmx->universe, &dmx->channel);
  if (dmx->channel == -1) {
    dmx->channel = dmx->universe;
    dmx->universe = 1;
  }
}

/*
 * msleep - sleep msec milliseconds
 */

int msleep(unsigned long msec) {
  struct timespec req = { 0 };
  time_t sec = (int) (msec / 1000);
  msec = msec - (sec * 1000);
  req.tv_sec = sec;
  req.tv_nsec = msec * 1000000L;
  while (nanosleep(&req, &req) == -1)
    continue;
  return 1;
}
