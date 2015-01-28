#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <config.h>

void config_parse_channels ( FILE *fp ) {

}

int main() {
  char *buf=NULL, *linestr, *leftstr, *rightstr;
  bool valid;
  size_t bufsize, len, nl=0;
  char fname[] = "conf/channels.conf";

  setlogmask(LOG_UPTO(LOG_DEBUG));
  openlog(DAEMON_NAME, LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID,
          LOG_USER);
  
  FILE *fp = fopen(fname, "r");
    
  if (!fp)
    exit(1);

  while ((len = getline(&buf, &bufsize, fp)) != -1) {
    nl++;
    config_strip_comment(buf);
    linestr = config_trim_string(buf);
    if (linestr != NULL) {
      valid = config_split_string(linestr, &leftstr, &rightstr);
      if (valid) {
	if (rightstr!=NULL) {
          printf("Config %s set to %s \n", leftstr, rightstr); 
          free(leftstr);
	  free(rightstr);
        } else {
          printf("Sectionheader %s \n", leftstr);
	  free(leftstr);
        }
      } else {
        syslog(LOG_ERR, "error in %s:%d", fname, nl);
      }
    }
    
  };
  
  free(buf);
  
  printf("Read %d lines \n", nl);
  
  fclose(fp);

  return 0;
  
}
