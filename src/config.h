#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <syslog.h>

#include <knxdmxd.h>

void config_strip_comment(char *str);
char *config_trim_string(char *str);
bool config_split_string(char *str, char **leftstr, char **rightstr);

#endif
