#include <config.h>
void config_strip_comment(char *str) {
  char *comment = strchr(str, '#');
  
  if (comment)
    *comment = 0;
  
}

char *config_trim_string(char *str) {
  char *endstr;

  while (isspace(*str)) 
    str++;
  
  if (*str == 0)
    return NULL;
 
  endstr = str + strlen(str) - 1;
  
  while ((endstr > str) && isspace(*endstr)) 
    endstr --;
  
  *(endstr+1) = 0;
  
  if (*str == 0)
    return NULL;
  else 
    return str;
}

bool config_split_string(char *str, char **leftstr, char **rightstr) {
  
  size_t len = strlen(str); 
  *leftstr = NULL;
  *rightstr = NULL;
   
  if (str[0] == '[') { // check if section
    if (str[len-1] != ']') 
      return false;

    *leftstr = (char *) calloc(len-2, sizeof(char));
    strncpy(*leftstr, str+1, len-2);
  } else { // option
      
    char *c = strchr(str, '=');
    
    if ((c==NULL) || (c==str))
      return false;
    
    *leftstr = (char *) calloc(c-str, sizeof(char));
    strncpy(*leftstr, str, c-str-1);
    *leftstr = config_trim_string(*leftstr);
 
    for (len=0; (*leftstr)[len]; len++)
      (*leftstr)[len] = tolower((*leftstr)[len]);

    c++;
    c = config_trim_string(c);

    if (c==NULL) 
      return false;
    
    *rightstr = (char *) calloc(strlen(c), sizeof(char));
    strcpy(*rightstr, c);
     
  }  
 
  return true;
  
}
