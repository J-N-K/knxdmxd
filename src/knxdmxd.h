#ifndef __KNXDMXD_H__
#define __KNXDMXD_H__

#define DAEMON_NAME "knxdmxd"
#define DAEMON_VERSION "1.0.2"

#include <e131.h>
#include <eibclient.h>
#include <syslog.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * TARGET_ constants
 */

#define TARGET_DIMMER 1
#define TARGET_SCENE 2
#define TARGET_CUE 3
#define TARGET_CUELIST 4

/*
 * TRIGGER_ constants
 */

#define TRIGGER_GO 1
#define TRIGGER_HALT 2
#define TRIGGER_RELEASE 3
#define TRIGGER_DIRECT 4
#define TRIGGER_VALUE 5
#define TRIGGER_FADING 6
#define TRIGGER_DIM 7
#define TRIGGER_SWITCH 8
#define TRIGGER_STEPDIM 9

/*
 * type definitions
 */

typedef struct {
  eibaddr_t ga;
  long value;
  char dpt; // 1 or 5 !
} knx_message_t;

typedef struct knx_message_queue_t_ knx_message_queue_t;

struct knx_message_queue_t_ {
  knx_message_t *msg;
  struct knx_message_queue_t_ *next;
};

typedef struct {
  eibaddr_t ga;
  char target_type;
  char trigger_type;
  size_t target;
  long value;
} trigger_t;

typedef struct {
  unsigned universe;
  int channel;
} dmxaddr_t;

typedef struct {
  char *name;
  dmxaddr_t dmx;
  float factor;
  eibaddr_t valuega, switchga;
} channel_t;

typedef struct {
  char *name;
  dmxaddr_t dmx;
  u_int8_t last_value, turnon_value;
  float fading, dimtime;
  bool process, direction;
  unsigned long next;
} dimmer_t;

typedef struct {
  dmxaddr_t dmx;
  unsigned char value;
} cue_channel_t;

typedef struct {
  char *name;
  float fade_in, fade_out;
  float waittime, delay;
  bool is_link, delay_on, process;
  unsigned char dim;
  unsigned long start; // time when cue should be sent to output
  cue_channel_t *data;
  size_t data_len;
 size_t cue_link;
} cue_t;

typedef struct {
  char *name;
  size_t cue_num, current_cue, next_cue;
  bool cue_running;
  unsigned long startnextcue, startcue; // time when next cue is called, time when current cue was called
  unsigned char dim;
  bool release_on_halt, proceed_on_go;
  cue_t *cues;
} cuelist_t;

typedef struct {
  unsigned char old[513], new[513];
  unsigned long start[513], stop[513];
  float factor[513];
  eibaddr_t statusvalue[513], statusswitch[513];
  E131_packet_t packet;
} universe_t;
  
#endif