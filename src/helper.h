/*
 * helper.h
 *
 * Created on: 2015-01-31
 *      
 * Author: Jan N. Klug
 * 
 */

#ifndef __HELPER_H__
#define __HELPER_H__

#include <string.h>
#include <knxdmxd.h>

eibaddr_t str2knx(const char *gastr);
void str2dmx(dmxaddr_t *dmx, const char *dmxstr);
bool strdcopy(char **dst, const char *src);

#endif