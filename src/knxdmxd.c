/*
 * knxdmxd.c
 * Copyright (C) Jan N. Klug 2011-2015 <jan.n.klug@rub.de>
 * Daemon skeleton by Michael Markstaller 2011 <devel@wiregate.de>
 *
 * knxdmxd is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * knxdmxd is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <syslog.h>
#include <json/json.h>
#include <pthread.h>
#include <stdbool.h>

#include <eibclient.h>
#include <e131.h>
#include <helper.h>
#include <knxdmxd.h>

#define USAGESTRING "\n"\
  "\t-d               Run as daemon/No debug output\n"\
  "\t-p <pidfile>     PID-filename\n"\
  "\t-u <eib url>     URL to contact eibd like local:/tmp/eib or ip:192.168.0.101\n"\
  "\t-c <config-file> Config-File\n"
#define NUM_THREADS 5

#define RETRY_TIME 5
#define BUFLEN 1024
#define POLLING_INTERVAL 10
#define MAX_UNIVERSES 16
#define DMX_INTERVAL 25 // in ms

#define TARGET_DIMMER 1
#define TARGET_SCENE 2
#define TARGET_CUE 3
#define TARGET_CUELIST 4

#define TRIGGER_GO 1
#define TRIGGER_HALT 2
#define TRIGGER_RELEASE 3
#define TRIGGER_DIRECT 4
#define TRIGGER_VALUE 5
#define TRIGGER_FADING 6
#define TRIGGER_DIM 7
#define TRIGGER_SWITCH 8
#define TRIGGER_STEPDIM 9

#define NOT_FOUND SIZE_MAX

/*#define true 1
#define false 0
typedef u_int8_t bool;
*/

const u_int8_t E131_packet_identifier[] = { 0x41, 0x53, 0x43, 0x2d, 0x45, 0x31,
    0x2e, 0x31, 0x37, 0x00, 0x00, 0x00 };

uuid_t E131_sender_UUID = { 0 };
char E131_UACN_basename[64];


void init_E131_packet(E131_packet_t *pkt, u_int16_t universe) {
  // Root Layer
  pkt->RL.preamble_size = htons(0x10);
  pkt->RL.postamble_size = 0x0000;
  memcpy(&pkt->RL.packet_ident, &E131_packet_identifier, 12);
  pkt->RL.flags_length = htons(28672 + 622);
  pkt->RL.vector = htonl(0x4);
  memcpy(&pkt->RL.CID, &E131_sender_UUID, sizeof(uuid_t));
  // Framing Layer sequence to be set later by sender
  pkt->FL.universe = htons(universe);
  pkt->FL.flags_length = htons(28672 + 600);
  pkt->FL.vector = htonl(0x2);
  gethostname(pkt->FL.source_name, 54);
  u_int8_t hnl =  strlen(pkt->FL.source_name);
  sprintf(pkt->FL.source_name+hnl, " - 0x%04x", universe);
  pkt->FL.priority = 100;
  pkt->FL.reserved = 0;
  pkt->FL.options = 0;
  // DMP Layer
  pkt->DMP.flags_length = htons(28672 + 523);
  pkt->DMP.vector = 0x2;
  pkt->DMP.address_type = 0xa1;
  pkt->DMP.first_address = 0;
  pkt->DMP.address_increment = htons(1);
  pkt->DMP.property_value_count = htons(513);
  bzero((char *) &pkt->DMP.property_values, 513);
}

void init_E131() {
  uuid_generate(E131_sender_UUID);
}




/*
 * variables for dmx
 */

u_int16_t universe_list[MAX_UNIVERSES + 1] = { 0 }; // max. 16 universes
size_t channel_num = 0, dimmer_num = 0, scene_num = 0, cuelist_num,
    trigger_num = 0, universe_num;
channel_t *channels;
dimmer_t *dimmers;
cue_t *scenes;
cuelist_t *cuelists;
universe_t *universes;
pthread_mutex_t universe_lock[MAX_UNIVERSES], knx_out_queue_lock, scene_lock,
    cuelist_lock, dimmer_lock;

trigger_t *triggers;

/*
 * variables for  general functions
 */

int pidFilehandle;
char *pidfilename = "/var/run/knxdmxd.pid";
char *eibd_url = "local:/tmp/eib";
char *conf_file = "knxdmxd.conf";
char *e131_receiver = NULL;
char *e131_sender = NULL;

knx_message_queue_t *knx_out_head = NULL, *knx_out_tail = NULL;
size_t knx_out_size = 0;


/*
 * knx_queue_append_message
 */

void knx_queue_append_message(knx_message_queue_t **head,
    knx_message_queue_t **tail, size_t *queue_size, knx_message_t *msg) {
  pthread_mutex_lock(&knx_out_queue_lock);
  if (*queue_size == 0) {
    *head = malloc(sizeof(knx_message_queue_t));
    (*head)->msg = msg;
    *tail = *head;
  } else {
    (*tail)->next = malloc(sizeof(knx_message_queue_t));
    if ((*tail)->next) {
      (*tail)->next->msg = msg;
      (*tail) = (*tail)->next;
    } else {
      syslog(LOG_WARNING,
          "knx_queue_append_message: could not allocate memory");
      free(msg);
    }
  }
  (*tail)->next = NULL;
  (*queue_size)++;
  pthread_mutex_unlock(&knx_out_queue_lock);
}

/*
 * knx_queue_remove_message
 */

void knx_queue_remove_message(knx_message_queue_t **head,
    knx_message_queue_t **tail, size_t *queue_size, knx_message_t **msg) {
  *msg = (*head)->msg;
  knx_message_queue_t *tmpPtr = (*head);
  (*head) = (*head)->next;
  free(tmpPtr);

  if ((*head) == NULL)
    (*tail) = NULL;

  (*queue_size)--;

}


/*
 * E131address - calculate Multicast IP for E1.31
 */

u_int32_t E131address(u_int16_t universe) {
  u_int8_t universe_hi, universe_lo;
  universe_hi = universe / 256;
  universe_lo = universe % 256;
  return 239 + 256 * (255 + 256 * (universe_hi + 256 * universe_lo));
}

/*
 * daemonShutdown - clear everything
 */

void daemonShutdown() {
  close(pidFilehandle);
  unlink(pidfilename);
  exit(EXIT_SUCCESS);
}

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

void signal_handler(int sig) {
  switch (sig) {
  case SIGHUP:
    syslog(LOG_WARNING, "signal_handler: received SIGHUP signal.");
    break;
  case SIGTERM:
    syslog(LOG_WARNING, "signal_handler: received SIGTERM signal.");
    daemonShutdown();
    break;
  case SIGINT:
    syslog(LOG_WARNING, "signal_handler: received SIGINT signal.");
    daemonShutdown();
    break;
  default:
    syslog(LOG_WARNING, "signal_handler: unhandled signal (%d) %s", sig,
        strsignal(sig));
    break;
  }
}

void *dmx_sender() {
  int16_t i, addrlen, cnt;
  struct sockaddr_in sockaddr;
  int sock;
  bool send_error = false;

  if (e131_receiver) {
    syslog(LOG_DEBUG, "dmx_sender: using unicast to %s", e131_receiver);
  } else {
    syslog(LOG_DEBUG, "dmx_sender: using standard multicast");
  }

  while (1) {
    bzero((char *) &sockaddr, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr.sin_port = htons(E131_MCAST_PORT);

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    addrlen = sizeof(sockaddr);
    if (e131_sender) {
      syslog(LOG_DEBUG, "dmx_sender: using %s as source address",
          e131_sender);
      inet_pton(AF_INET, e131_sender, &(sockaddr.sin_addr.s_addr));
      bind(sock, (struct sockaddr *) &sockaddr, addrlen);
    }
    if (sock < 0) {
      syslog(LOG_ERR,
          "init_network: cannot open UDP socket for transmission");
      sleep(RETRY_TIME);
      continue;
    }

    addrlen = sizeof(sockaddr);
    while (!send_error) {
      for (i = 1; i < universe_num + 1; i++) {
        if (e131_receiver) {
          inet_pton(AF_INET, e131_receiver,
              &(sockaddr.sin_addr.s_addr));
        } else {
          sockaddr.sin_addr.s_addr = E131address(universe_list[i]); // 0 is empty universe, do not send
        }
        pthread_mutex_lock(&universe_lock[i]);
        universes[i].packet.FL.sequence_number++; // increase sequence number, overflow ok
        cnt = sendto(sock, &universes[i].packet, sizeof(E131_packet_t),
            0, (struct sockaddr *) &sockaddr, addrlen);
        pthread_mutex_unlock(&universe_lock[i]);
        if (cnt < 0) {
          syslog(LOG_ERR, "Could not send UDP data, closing socket");
          close(sock);
          send_error = true;
          break;
        }
      }

      msleep(DMX_INTERVAL);
    }
  }
}

void *knx_sender() {
  int len;
  EIBConnection *con;
  unsigned char buf[255];
  knx_message_t *msg;

  while (1) {
    usleep(25000);
    pthread_mutex_lock(&knx_out_queue_lock);
    if (knx_out_size > 0) {
      knx_queue_remove_message(&knx_out_head, &knx_out_tail,
          &knx_out_size, &msg);
      pthread_mutex_unlock(&knx_out_queue_lock);
      syslog(LOG_DEBUG,
          "knx_sender: processing message ga=%d dpt=%d value=%ld",
          msg->ga, msg->dpt, msg->value);
      con = EIBSocketURL(eibd_url);
      if (!con) {
        syslog(LOG_WARNING,
            "knx_sender: connection to eibd failed, message not sent");

        sleep(RETRY_TIME);
        continue;
      }
      if (EIBOpenT_Group(con, msg->ga, 1) == -1) {
        syslog(LOG_WARNING,
            "knx_sender: socket failed, message not sent");
        sleep(RETRY_TIME);
        continue;
      }

      len = 0;

      buf[0] = 0;
      buf[1] = 0x80;

      switch (msg->dpt) {
      case 1:
        buf[1] |= msg->value & 0x3f;
        len = 2;
        break;
      case 5:
        buf[2] = msg->value;
        len = 3;
        break;
      }
      len = EIBSendAPDU(con, len, buf);
      if (len == -1) {
        syslog(LOG_WARNING, "knx_sender: failed to sent message");

      } else {
        syslog(LOG_DEBUG, "knx_sender: sent message");

      }
      EIBClose(con);
      free(msg);

    } else {
      pthread_mutex_unlock(&knx_out_queue_lock);
    }

  }
}

size_t find_first_trigger(eibaddr_t in, size_t first, size_t last) {
  if (in == triggers[first].ga) {
    while ((--first) >= 0) { // look if same address is before
      if (triggers[first].ga != in) {
        break;
      }
    }
    return (first + 1);
  }
  if (last > first + 1) {
    size_t middle = (last + first) / 2;
    if (in >= triggers[middle].ga) {
      return find_first_trigger(in, middle, last);
    }
    if (in < triggers[middle].ga) {
      return find_first_trigger(in, first, middle);
    }
  }
  return NOT_FOUND;
}

void set_dmx_channel(u_int16_t u, u_int16_t c, u_int8_t value, float fade_in,
    float fade_out) {
  struct timeb t;
  pthread_mutex_lock(&universe_lock[u]);
  universes[u].new[c] =
      (unsigned char) (value * universes[u].factor[c] + 0.5);
  universes[u].old[c] = universes[u].packet.DMP.property_values[c];
  float fadeTime =
      (universes[u].new[c] > universes[u].old[c]) ? fade_in : fade_out;
  ftime(&t);
  universes[u].start[c] = t.time * 1000 + t.millitm;
  universes[u].stop[c] = universes[u].start[c]
      + (unsigned long) (fadeTime * 1000.0);
  pthread_mutex_unlock(&universe_lock[u]);
}

void increase_dmx_channel(u_int16_t u, u_int16_t c, int value) {
  struct timeb t;
  float factor = universes[u].factor[c];

  pthread_mutex_lock(&universe_lock[u]);
  u_int8_t current = universes[u].packet.DMP.property_values[c];
  int newval = (int) (((current / factor) + value) * factor + 0.5);
  newval = newval < 0 ? 0 : newval; // range check
  newval = newval > 255 ? 255 : newval;
  universes[u].new[c] = (u_int8_t) newval;
  universes[u].old[c] = (u_int8_t) newval;
  universes[u].packet.DMP.property_values[c] = (u_int8_t) newval;
  ftime(&t);
  universes[u].start[c] = t.time * 1000 + t.millitm;
  universes[u].stop[c] = t.time * 1000 + t.millitm;
  pthread_mutex_unlock(&universe_lock[u]);
}

u_int8_t get_dmx_channel(u_int16_t u, u_int16_t c) {
  u_int8_t value;
  pthread_mutex_lock(&universe_lock[u]);
  value = universes[u].packet.DMP.property_values[c];
  pthread_mutex_unlock(&universe_lock[u]);
  return value;
}

void call_cue(cue_t *cue) {
  size_t i;
  float in = cue->fade_in, out = cue->fade_out;
  for (i = 0; i < cue->data_len; i++) {
    set_dmx_channel(cue->data[i].dmx.universe, cue->data[i].dmx.channel,
        (unsigned char) (cue->data[i].value * cue->dim / 255), in, out);
  }
  syslog(LOG_DEBUG, "call_cue: called cue %s", cue->name);
}

void send_channel_status(u_int16_t u, u_int16_t c) {
  if (universes[u].statusvalue[c]) {
    syslog(LOG_DEBUG, "send_channel_status: sending status to ga %u",
        universes[u].statusvalue[c]);
    knx_message_t *msg;
    msg = (knx_message_t*) calloc(1, sizeof(*msg));
    if (msg == NULL) {
      syslog(LOG_WARNING,
          "send_channel_status: could not allocate memory for status message");
    }
    msg->ga = universes[u].statusvalue[c];
    msg->dpt = 5;
    msg->value = (unsigned char) (universes[u].new[c]
        / universes[u].factor[c] + 0.5);

    knx_queue_append_message(&knx_out_head, &knx_out_tail, &knx_out_size,
        msg);
  }
  if (universes[u].statusswitch[c]) {
    syslog(LOG_DEBUG, "send_channel_status: sending on/off to ga %u",
        universes[u].statusswitch[c]);
    knx_message_t *msg;
    msg = (knx_message_t*) calloc(1, sizeof(*msg));
    if (msg == NULL) {
      syslog(LOG_WARNING,
          "send_channel_status: could not allocate memory for status message");
    }
    msg->ga = universes[u].statusswitch[c];
    msg->dpt = 1;
    msg->value = (unsigned char) (universes[u].new[c] > 0);

    knx_queue_append_message(&knx_out_head, &knx_out_tail, &knx_out_size,
        msg);
  }

}

void *cue_processor() {
  size_t i;
  size_t current_cue;
  dimmer_t *dimmer;
  while (1) {
    struct timeb t;
    ftime(&t);
    unsigned long currenttime = t.time * 1000 + t.millitm;

    for (i = 0; i < dimmer_num; i++) {
      pthread_mutex_lock(&dimmer_lock);
      dimmer = &dimmers[i];
      if ((dimmer->process) && (currenttime >= dimmer->next)) {
        int inc = (dimmer->direction ? 1 : -1) * 1;
        dimmer->next = (unsigned long) (currenttime
            + 1000.0 * (dimmer->dimtime / 255.0)); // set next step
        increase_dmx_channel(dimmer->dmx.universe,
            dimmer->dmx.channel, inc);
      }
      pthread_mutex_unlock(&dimmer_lock);

    }

    for (i = 0; i < scene_num; i++) {
      pthread_mutex_lock(&scene_lock);
      if ((scenes[i].process) && (currenttime >= scenes[i].start)) {
        scenes[i].process = false;
        pthread_mutex_unlock(&scene_lock);
        call_cue(&scenes[i]);
      } else {
        pthread_mutex_unlock(&scene_lock);
      }

    }

    for (i = 0; i < cuelist_num; i++) {
      pthread_mutex_lock(&cuelist_lock);
      if (cuelists[i].cue_running) {
        current_cue = cuelists[i].current_cue;
        if ((currenttime >= cuelists[i].cues[current_cue].start)
            && (cuelists[i].cues[current_cue].process)) { // cue ready (delay passed)
          cuelists[i].cues[current_cue].process = false;
          pthread_mutex_unlock(&cuelist_lock); // unlock for sending output
          cuelists[i].cues[current_cue].dim = cuelists[i].dim; // update cue dim to cuelist dim
          call_cue(&cuelists[i].cues[current_cue]);
          pthread_mutex_lock(&cuelist_lock);
        }
        if (currenttime >= cuelists[i].startnextcue) { // we have to call the next cue
          cuelists[i].current_cue = cuelists[i].next_cue; // make next cue current cue and find next
          if (cuelists[i].current_cue < (cuelists[i].cue_num - 1)) { // still not at the end of the cuelist
            size_t next;
            next = cuelists[i].current_cue + 1;
            if (cuelists[i].cues[next].is_link) { // we have a link
              next = cuelists[i].cues[next].cue_link;
              cuelists[i].next_cue = next;
              syslog(LOG_DEBUG,
                  "cue_processor: cuelist %s will advance via link from %d to cue %d ",
                  cuelists[i].name, cuelists[i].current_cue,
                  cuelists[i].next_cue);
            } else { // normal cue
              cuelists[i].next_cue = next;
              syslog(LOG_DEBUG,
                  "cue_processor: %lu cuelist %s will advance to cue %d ",
                  currenttime, cuelists[i].name,
                  cuelists[i].next_cue);
            }
            cuelists[i].startnextcue = currenttime
                + 1000. * cuelists[i].cues[next].waittime;
            cuelists[i].cues[cuelists[i].current_cue].start =
                currenttime
                    + 1000.
                        * cuelists[i].cues[cuelists[i].current_cue].delay;
            cuelists[i].cues[cuelists[i].current_cue].process =
            true;
            cuelists[i].cues[next].process = true;
          } else { // we are at the end of the cuelist

            if (!cuelists[i].cues[cuelists[i].current_cue].process) { // last cue has been sent to output, stop cuelist
              cuelists[i].cue_running = false;
              syslog(LOG_DEBUG,
                  "cue_processor: cuelist %s stopped",
                  cuelists[i].name);
              if (cuelists[i].release_on_halt) { // release
                cuelists[i].current_cue = 0;
                syslog(LOG_DEBUG,
                    "cue_processor: cuelist %s released",
                    cuelists[i].name);
              }
            }
          }
        }
        pthread_mutex_unlock(&cuelist_lock);

      } else {
        pthread_mutex_unlock(&cuelist_lock);
      }
    }
    msleep(20);
  }
}

void *crossfade_processor() {
  size_t u;
  while (1) {
    for (u = 1; u < universe_num + 1; u++) {
      int i;
      int max = 513;
      struct timeb t;
      ftime(&t);
      unsigned long currenttime = t.time * 1000 + t.millitm;

      for (i = 512; i >= 1; max = i, i--) // find last value
        if (universes[u].new[i] || universes[u].old[i])
          break;

      for (i = 1; i < max; i++) {
        if ((universes[u].new[i] || universes[u].old[i])
            && (universes[u].new[i] != universes[u].old[i])) { /* avoid calculating with only 0 or finished */
          if (currenttime > universes[u].stop[i]) {
            universes[u].packet.DMP.property_values[i] =
                universes[u].old[i] = universes[u].new[i];
            syslog(LOG_DEBUG,
                "crossfade_processor: finished crossfading %d.%d to %d",
                universe_list[u], i, universes[u].new[i]);
            send_channel_status(u, i);
          } else {
            float p =
                (float) (currenttime - universes[u].start[i])
                    / (universes[u].stop[i]
                        - universes[u].start[i]);
            float q = 1.0f - p;
            universes[u].packet.DMP.property_values[i] =
                (unsigned char) (universes[u].old[i] * q
                    + universes[u].new[i] * p);

          }
        }
      }
    }
    msleep(DMX_INTERVAL);
  }
}

void *knx_receiver() {
  int len;
  EIBConnection *con;
  eibaddr_t dest;
  eibaddr_t src;
  unsigned char buf[255], val;

  while (1) { //retry infinite
    con = EIBSocketURL(eibd_url);
    if (!con) {
      syslog(LOG_WARNING, "knx_receiver: connection to eibd failed");

      sleep(RETRY_TIME);
      continue;
    }

    if (EIBOpen_GroupSocket(con, 0) == -1) {
      syslog(LOG_WARNING, "knx_receiver: socket failed");
      sleep(RETRY_TIME);
      continue;
    }

    while (1) {
      len = EIBGetGroup_Src(con, sizeof(buf), buf, &src, &dest);

      if (len == -1) {
        syslog(LOG_WARNING, "knx_receiver: read failed");
        sleep(RETRY_TIME);
        break;
      }

      if (len < 2) {
        syslog(LOG_WARNING, "knx_receiver: invalid packet");
        break;
      }

      if ((buf[0] & 0x3) || (buf[1] & 0xC0) == 0xC0) {
        syslog(LOG_WARNING, "knx_receiver: unknown APDU from %d to %d",
            src, dest);
        break;
      } else {
        switch (buf[1] & 0xC0) {
        case 0x00:
          break;
        case 0x40:
          //FIXME: response dunno
          break;
        case 0x80:
          if (buf[1] & 0xC0) {
            val = (len == 2) ? buf[1] & 0x3F : buf[2];
            size_t trigger =
                ((dest > triggers[trigger_num - 1].ga)
                    || (dest < triggers[0].ga)) ?
                    -1 :
                    find_first_trigger(dest, 0,
                        trigger_num);
            if (trigger != NOT_FOUND) {
              struct timeb t;
              ftime(&t);
              unsigned long currenttime = t.time * 1000
                  + t.millitm;

              do {
                syslog(LOG_DEBUG,
                    "knx_receiver: trigger %u (GA:%u) triggered target %u (type %u)",
                    trigger, triggers[trigger].ga,
                    triggers[trigger].target,
                    triggers[trigger].target_type);
                switch (triggers[trigger].target_type) { // first target type
                case TARGET_DIMMER:
                  switch (triggers[trigger].trigger_type) { // trigger type
                  case TRIGGER_VALUE:
                    set_dmx_channel(
                        dimmers[triggers[trigger].target].dmx.universe,
                        dimmers[triggers[trigger].target].dmx.channel,
                        val,
                        dimmers[triggers[trigger].target].fading,
                        dimmers[triggers[trigger].target].fading);
                    syslog(LOG_DEBUG,
                        "knx_receiver: dimmer value trigger: %s = %d",
                        dimmers[triggers[trigger].target].name,
                        val);
                    break;
                  case TRIGGER_SWITCH:
                    if (val == 0) {
                      // save current value and turn off
                      dimmers[triggers[trigger].target].last_value =
                          dimmers[triggers[trigger].target].turnon_value ?
                              dimmers[triggers[trigger].target].turnon_value :
                              get_dmx_channel(
                                  dimmers[triggers[trigger].target].dmx.universe,
                                  dimmers[triggers[trigger].target].dmx.channel);
                      set_dmx_channel(
                          dimmers[triggers[trigger].target].dmx.universe,
                          dimmers[triggers[trigger].target].dmx.channel,
                          0,
                          dimmers[triggers[trigger].target].fading,
                          dimmers[triggers[trigger].target].fading);
                      syslog(LOG_DEBUG,
                          "knx_receiver: switch trigger %s off (next turn-on value %d)",
                          dimmers[triggers[trigger].target].name,
                          dimmers[triggers[trigger].target].last_value);
                    } else {
                      // use saved value and turn on
                      set_dmx_channel(
                          dimmers[triggers[trigger].target].dmx.universe,
                          dimmers[triggers[trigger].target].dmx.channel,
                          dimmers[triggers[trigger].target].last_value,
                          dimmers[triggers[trigger].target].fading,
                          dimmers[triggers[trigger].target].fading);
                      syslog(LOG_DEBUG,
                          "knx_receiver: switch trigger %s on (turn-on value %d)",
                          dimmers[triggers[trigger].target].name,
                          dimmers[triggers[trigger].target].last_value);
                    }
                    break;
                  case TRIGGER_STEPDIM:
                    if (val == 0) { // stop dimming
                      pthread_mutex_lock(&dimmer_lock);
                      dimmers[triggers[trigger].target].process =
                      FALSE;
                      pthread_mutex_unlock(&dimmer_lock);

                      syslog(LOG_DEBUG,
                          "knx_receiver: 4-bit dim %s, stop, value = %d",
                          dimmers[triggers[trigger].target].name,
                          get_dmx_channel(
                              dimmers[triggers[trigger].target].dmx.universe,
                              dimmers[triggers[trigger].target].dmx.channel));
                      send_channel_status(
                          dimmers[triggers[trigger].target].dmx.universe,
                          dimmers[triggers[trigger].target].dmx.channel);
                    } else {
                      bool direction = (val & 0x08) > 0;
                      u_int8_t step = (val & 0x7);
                      pthread_mutex_lock(&dimmer_lock);
                      dimmers[triggers[trigger].target].process =
                      TRUE;
                      dimmers[triggers[trigger].target].direction =
                          direction;
                      dimmers[triggers[trigger].target].next =
                          currenttime;
                      pthread_mutex_unlock(&dimmer_lock);
                      syslog(LOG_DEBUG,
                          "knx_receiver: 4-bit dim %s, direction %s, stepsize %d",
                          dimmers[triggers[trigger].target].name,
                          (direction ? "up" : "down"),
                          step);
                    }
                    break;
                  default:
                    syslog(LOG_INFO,
                        "knx_receiver: dimmer trigger: unknown trigger type %d",
                        triggers[trigger].trigger_type);
                    break;
                  }
                  break;
                case TARGET_SCENE:
                  switch (triggers[trigger].trigger_type) {
                  case TRIGGER_GO:
                    if (triggers[trigger].value == val) {
                      pthread_mutex_lock(&scene_lock);
                      scenes[triggers[trigger].target].process =
                      true;
                      scenes[triggers[trigger].target].start =
                          currenttime
                              + 1000.
                                  * scenes[triggers[trigger].target].delay;
                      pthread_mutex_unlock(&scene_lock);
                    }
                    break;
                  default:
                    syslog(LOG_INFO,
                        "knx_receiver: scene trigger: unknown trigger type %d",
                        triggers[trigger].trigger_type);
                    break;
                  }
                  break;
                case TARGET_CUELIST:
                  syslog(LOG_DEBUG,
                      "knx_receiver: cuelists trigger %d (value %d / %ld)",
                      triggers[trigger].trigger_type, val,
                      triggers[trigger].value);
                  switch (triggers[trigger].trigger_type) {
                  case TRIGGER_GO:
                    if (triggers[trigger].value == val) {
                      pthread_mutex_lock(&cuelist_lock);
                      if (cuelists[triggers[trigger].target].cue_running) {

                      } else {
                        cuelists[triggers[trigger].target].cue_running =
                        true;
                        cuelists[triggers[trigger].target].startnextcue =
                            currenttime;
                      }
                      pthread_mutex_unlock(&cuelist_lock);
                    }
                    break;
                  case TRIGGER_RELEASE:
                    if (triggers[trigger].value == val) {
                      pthread_mutex_lock(&cuelist_lock);
                      cuelists[triggers[trigger].target].cue_running =
                      false;
                      cuelists[triggers[trigger].target].current_cue =
                          0;
                      if (cuelists[triggers[trigger].target].cues[0].is_link) {
                        cuelists[triggers[trigger].target].next_cue =
                            cuelists[triggers[trigger].target].cues[0].cue_link;
                      } else {
                        cuelists[triggers[trigger].target].next_cue =
                            1;
                      }
                      pthread_mutex_unlock(&cuelist_lock);
                    }
                    break;
                  case TRIGGER_HALT:
                    if (triggers[trigger].value == val) {
                      pthread_mutex_lock(&cuelist_lock);
                      cuelists[triggers[trigger].target].cue_running =
                      false;
                      pthread_mutex_unlock(&cuelist_lock);
                    }
                    break;
                  case TRIGGER_DIRECT:
                    if (val
                        < cuelists[triggers[trigger].target].cue_num) {
                      pthread_mutex_lock(&cuelist_lock);
                      cuelists[triggers[trigger].target].cue_running =
                      true;

                      if (cuelists[triggers[trigger].target].cues[val].is_link) {
                        cuelists[triggers[trigger].target].next_cue =
                            cuelists[triggers[trigger].target].cues[val].cue_link;
                      } else {
                        cuelists[triggers[trigger].target].next_cue =
                            val;
                      }
                      cuelists[triggers[trigger].target].cues[cuelists[triggers[trigger].target].current_cue].process =
                      false;
                      cuelists[triggers[trigger].target].cues[cuelists[triggers[trigger].target].next_cue].process =
                      true;
                      cuelists[triggers[trigger].target].cues[cuelists[triggers[trigger].target].next_cue].start =
                          currenttime;
                      pthread_mutex_unlock(&cuelist_lock);
                    }
                    break;
                  default:
                    syslog(LOG_INFO,
                        "knx_receiver: cuelist trigger: unknown trigger type %d",
                        triggers[trigger].trigger_type);
                    break;
                  }
                  break;
                default:
                  break;
                }
                if ((++trigger) > trigger_num)
                  break;
              } while (triggers[trigger].ga == dest);

            }

          }
          break;
        }
      }
    }

    syslog(LOG_WARNING, "knx_receiver: eibd: closed connection");
    EIBClose(con);
  }
}

bool json_get_trigger(struct json_object *trigger, const char trigger_type,
    const char target_type, const size_t target) {
  if (!trigger) {
    return false;
  }

  struct json_object *trigger_knx, *trigger_value;

  switch (trigger_type) {
  case TRIGGER_VALUE:
  case TRIGGER_SWITCH:
  case TRIGGER_STEPDIM:
    trigger_knx = trigger;
    trigger_value = NULL;
    break;
  case TRIGGER_FADING:
    trigger_knx = json_object_object_get(trigger, "fadingga");
    trigger_value = json_object_object_get(trigger, "value");
    break;
  default:
    trigger_knx = json_object_object_get(trigger, "knx");
    trigger_value = json_object_object_get(trigger, "value");
    break;
  }

  if (!trigger_knx) {
    return false;
  }

  if (trigger_num % 20 == 0) {
    trigger_t *old = triggers;
    triggers = calloc(trigger_num + 20, sizeof(*triggers));
    if (triggers == NULL) {
      syslog(LOG_ERR,
          "json_get_trigger: could not allocate memory for next trigger");
      exit(EXIT_FAILURE);
    }
    memcpy(triggers, old, trigger_num * sizeof(*triggers));
    free(old);
  }

  triggers[trigger_num].ga = str2knx(json_object_get_string(trigger_knx));
  if (trigger_value) {
    triggers[trigger_num].value = json_object_get_int(trigger_value);
  } else {
    triggers[trigger_num].value = -1;
  }
  triggers[trigger_num].trigger_type = trigger_type;
  triggers[trigger_num].target_type = target_type;
  triggers[trigger_num].target = target;

  trigger_num++;

  return true;
}

bool json_get_cue(cue_t *cue, struct json_object *json_cue, const int cuenum,
    const int cue_type) {

  int i;

  struct json_object *name = json_object_object_get(json_cue, "name");
  strdcopy(&cue->name, (name) ? json_object_get_string(name) : "unknown");

// check link first
  struct json_object *link = json_object_object_get(json_cue, "link");

  if (link) { // links not allowed for scenes
    if (cue_type == TARGET_SCENE) {
      syslog(LOG_INFO,
          "json_get_cue: detected link-type element in scene %s, ignored",
          cue->name);
    } else {
      free(cue->name);
      strdcopy(&cue->name, json_object_get_string(link));
      cue->is_link = true;
      syslog(LOG_DEBUG, "json_get_cue: created link to cue %s",
          cue->name);
      return true;
    }
  } else {
    cue->is_link = false;
  }

  struct json_object *json_channels = json_object_object_get(json_cue,
      "data");
  if (!json_channels) {
    syslog(LOG_INFO,
        "json_get_cue: skipping cue/scene %s (no channels defined)",
        cue->name);
    return false;
  }

  cue->data_len = json_object_array_length(json_channels);
  cue->data = (cue_channel_t*) calloc(cue->data_len, sizeof(*(cue->data)));
  if (cue->data == NULL) {
    syslog(LOG_ERR,
        "json_get_cue: could not allocate memory for cue channel definitions");
    exit(EXIT_FAILURE);
  }

  cue->dim = 255; // default is cue full on

  for (i = 0; i < cue->data_len; i++) { // read all
                      // get channel
    struct json_object *channel = json_object_array_get_idx(json_channels,
        i);
    struct json_object *chan = json_object_object_get(channel, "channel");
    struct json_object *value = json_object_object_get(channel, "value");

    if ((!chan) || (!value)) {
      syslog(LOG_INFO,
          "json_get_cue: skipping errorneous channel def %d in cue %s",
          i, cue->name);
      cue->data[i].dmx.universe = 0;
      continue;
    }

    //lookup channel name
    int j;
    cue->data[i].dmx.universe = 0;
    const char *ch = json_object_get_string(chan);
    for (j = 0; j < channel_num; j++) {
      if (strcmp(channels[j].name, ch) == 0) {
        cue->data[i].dmx.universe = channels[j].dmx.universe;
        cue->data[i].dmx.channel = channels[j].dmx.channel;
        break;
      }
    }

    cue->data[i].value = json_object_get_int(value);
    syslog(LOG_DEBUG, "json_get_cue: cue %s DMX: %d/%d val: %d", cue->name,
        universe_list[cue->data[i].dmx.universe],
        cue->data[i].dmx.channel, cue->data[i].value);

  }

  struct json_object *fading = json_object_object_get(json_cue, "fading");
  if (fading) {
    struct json_object *fading_time = json_object_object_get(fading,
        "time");
    if (!fading_time) {
      struct json_object *fading_time_in = json_object_object_get(fading,
          "in");
      struct json_object *fading_time_out = json_object_object_get(fading,
          "out");

      if ((!fading_time_in) || (!fading_time_out)) {
        syslog(LOG_INFO,
            "json_get_cue: skipping errorneous fading def in cue %s",
            cue->name);
      } else {
        cue->fade_in = json_object_get_double(fading_time_in);
        cue->fade_out = json_object_get_double(fading_time_out);
        syslog(LOG_DEBUG,
            "json_get_cue: set fading to %.1f/%.1f in cue %s",
            cue->fade_in, cue->fade_out, cue->name);
      }
    } else {
      cue->fade_in = json_object_get_double(fading_time);
      cue->fade_out = json_object_get_double(fading_time);
      syslog(LOG_DEBUG, "json_get_cue: set fading to %.1f in cue %s",
          cue->fade_in, cue->name);
    }
  }

  struct json_object *waittime = json_object_object_get(json_cue, "waittime");
  if (waittime) {
    cue->waittime = json_object_get_double(waittime);
    syslog(LOG_DEBUG, "json_get_cue: set wait time to %.1f in cue %s",
        cue->waittime, cue->name);

  }

  struct json_object *delay = json_object_object_get(json_cue, "delay");
  if (delay) {
    cue->delay = json_object_get_double(delay);
    syslog(LOG_DEBUG, "json_get_cue: set delay to %.1f in cue %s",
        cue->delay, cue->name);
  }

  return true;
}

int trigger_compare(const void * a, const void * b) {
  return ((*(trigger_t*) a).ga - (*(trigger_t*) b).ga);
}

void load_config() {

  struct json_object *config, *in_data;
  size_t i, j;

  //FILE *pFile = fopen(conf_file, "r");

  if (access(conf_file, R_OK) != 0) {
    syslog(LOG_ERR,
        "load_config: error opening %s, check position and/or permissions ",
        conf_file);
    exit(EXIT_FAILURE);
  }

  config = json_object_from_file(conf_file);

  /*
   * channel definitions
   */

  in_data = json_object_object_get(config, "channels");
  channel_num = json_object_array_length(in_data);
  syslog(LOG_DEBUG, "load_config: trying to import %d channel(s)",
      channel_num);

  channels = (channel_t*) calloc(channel_num, sizeof(*channels));
  if (channels == NULL) {
    syslog(LOG_ERR,
        "load_config: could not allocate memory for channel definitions");
    exit(EXIT_FAILURE);
  }

  universe_num = 0;
  for (i = 0; i < channel_num; i++) { // read all
    struct json_object *element = json_object_array_get_idx(in_data, i);
    struct json_object *name = json_object_object_get(element, "name");
    struct json_object *dmx = json_object_object_get(element, "dmx");
    struct json_object *statusga = json_object_object_get(element,
        "statusga");

    struct json_object *factor = json_object_object_get(element, "factor");

    if ((!name) || (!dmx)) { // make illegal entries
      channels[i].valuega = 0;
      channels[i].switchga = 0;
      channels[i].dmx.universe = 0;
      channels[i].name = NULL;
      syslog(LOG_INFO,
          "load_config: skipping channel definition %d, missing name or DMX",
          i + 1);
      continue;
    }

    if (!strdcopy(&channels[i].name, json_object_get_string(name))) {
      syslog(LOG_ERR,
          "load_config: could not allocate memory for name of channel definition %d",
          i);
      exit(EXIT_FAILURE);
    }

    str2dmx(&channels[i].dmx, json_object_get_string(dmx));
    if (statusga) {
      if (json_object_get_type(statusga) == json_type_string) { // old style config, value only
        channels[i].valuega =
            statusga ?
                str2knx(json_object_get_string(statusga)) : 0;
        syslog(LOG_INFO,
            "load_config: old-style/deprecated statusga detected in channel %s, see documentation ",
            channels[i].name);
      } else {
        struct json_object *valuega = json_object_object_get(statusga,
            "value");
        channels[i].valuega =
            valuega ? str2knx(json_object_get_string(valuega)) : 0;
        struct json_object *switchga = json_object_object_get(statusga,
            "switch");
        channels[i].switchga =
            switchga ?
                str2knx(json_object_get_string(switchga)) : 0;
      }
    }

    if (factor) {
      channels[i].factor = json_object_get_double(factor);
    } else {
      channels[i].factor = 1.0;
    }

    bool uf = false;
    for (j = 1; j < universe_num + 1; j++) {
      if (universe_list[j] == channels[i].dmx.universe) {
        channels[i].dmx.universe = j;
        uf = true;
        break;
      }
    }

    if (!uf) { // we have a new universe
      if (universe_num >= MAX_UNIVERSES) {
        syslog(LOG_ERR,
            "load_config: max. number of universes allowed is %d",
            MAX_UNIVERSES);
        exit(EXIT_FAILURE);
      } else {
        universe_list[universe_num + 1] = channels[i].dmx.universe;
        universe_num++;
        channels[i].dmx.universe = universe_num;
      }
    }

    syslog(LOG_DEBUG,
        "load_config: named DMX %d/%d as %s, status: %d/%d, factor: %f",
        universe_list[channels[i].dmx.universe],
        channels[i].dmx.channel, channels[i].name, channels[i].switchga,
        channels[i].valuega, channels[i].factor);
  }

  /*
   * create universes
   */

  syslog(LOG_DEBUG, "load_config: allocating memory for %d universe(s)",
      universe_num);
  universes = (universe_t*) calloc(universe_num + 1, sizeof(*universes));
  if (universes == NULL) {
    syslog(LOG_ERR,
        "load_config: could not allocate memory for universe buffer(s)");
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < universe_num; i++) {
    init_E131_packet(&universes[i + 1].packet, universe_list[i + 1]); // 0 is the empty universe
    bzero((char *) &universes[i + 1].new, 513);
    bzero((char *) &universes[i + 1].old, 513);
    bzero((unsigned long *) &universes[i + 1].start, 513);
    bzero((unsigned long *) &universes[i + 1].stop, 513);
    bzero((eibaddr_t *) &universes[i + 1].statusvalue, 513);
    bzero((eibaddr_t *) &universes[i + 1].statusswitch, 513);
    if (pthread_mutex_init(&universe_lock[i + 1], NULL) != 0) {
      syslog(LOG_ERR,
          "load_config: could not initalize mutex lock for universe %d",
          universe_list[i + 1]);
      exit(EXIT_FAILURE);
    }
    syslog(LOG_DEBUG, "load_config: created universe %d",
        universe_list[i + 1]);
  }

  /*
   * set status ga
   */

  for (i = 0; i < channel_num; i++) {
    universes[channels[i].dmx.universe].statusvalue[channels[i].dmx.channel] =
        channels[i].valuega;
    universes[channels[i].dmx.universe].statusswitch[channels[i].dmx.channel] =
        channels[i].switchga;
    universes[channels[i].dmx.universe].factor[channels[i].dmx.channel] =
        channels[i].factor;
  }

  /*
   * dimmers
   */

  in_data = json_object_object_get(config, "dimmers");
  dimmer_num = json_object_array_length(in_data);
  syslog(LOG_DEBUG, "load_config: trying to import %d dimmer(s)", dimmer_num);

  dimmers = (dimmer_t*) calloc(dimmer_num, sizeof(*dimmers));
  if (dimmers == NULL) {
    syslog(LOG_ERR,
        "load_config: could not allocate memory for dimmer definitions");
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < dimmer_num; i++) { // read all

    struct json_object *element = json_object_array_get_idx(in_data, i);

    dimmers[i].process = FALSE; // disable processing

    // get name & create
    struct json_object *name = json_object_object_get(element, "name");
    struct json_object *dmx = json_object_object_get(element, "channel");
    struct json_object *ga = json_object_object_get(element, "ga");

    if ((!name) || (!dmx) || (!ga)) {
      dimmers[i].dmx.universe = 0;
      dimmers[i].name = NULL;
      syslog(LOG_INFO,
          "load_config : skipping dimmer definition %d, missing name, channels or GA",
          i + 1);
      continue;
    }

    strdcopy(&dimmers[i].name, json_object_get_string(name));

    // lookup channel_name
    dimmers[i].dmx.universe = 0;
    const char *ch = json_object_get_string(dmx);
    for (j = 0; j < channel_num; j++) {
      if (strcmp(channels[j].name, ch) == 0) {
        dimmers[i].dmx.universe = channels[j].dmx.universe;
        dimmers[i].dmx.channel = channels[j].dmx.channel;
        break;
      }
    }
    if (dimmers[i].dmx.universe == 0) {
      dimmers[i].name = NULL;
      syslog(LOG_INFO,
          "load_config: skipping dimmer definition %d, invalid channel %s",
          i + 1, ch);
      continue;
    }

    if (json_object_get_type(ga) == json_type_string) { // old style config, value only
      syslog(LOG_INFO,
          "load_config: old-style/deprecated control GA detected in dimmer %s, see documentation ",
          dimmers[i].name);
      if (json_get_trigger(ga, TRIGGER_VALUE, TARGET_DIMMER, i)) {
        syslog(LOG_DEBUG,
            "load_config: created value trigger for dimmer %s",
            dimmers[i].name);
      } else {
        syslog(LOG_INFO,
            "load_config: failed to create trigger for dimmer %s",
            dimmers[i].name);
      }
    } else {
      struct json_object *valuega = json_object_object_get(ga, "value");
      struct json_object *switchga = json_object_object_get(ga, "switch");
      struct json_object *stepga = json_object_object_get(ga, "dim");
      if (json_get_trigger(valuega, TRIGGER_VALUE, TARGET_DIMMER, i)) {
        syslog(LOG_DEBUG,
            "load_config: created value trigger for dimmer %s",
            dimmers[i].name);
      } else {
        syslog(LOG_INFO,
            "load_config: failed to create value trigger for dimmer %s",
            dimmers[i].name);
      }
      if (json_get_trigger(switchga, TRIGGER_SWITCH, TARGET_DIMMER, i)) {
        syslog(LOG_DEBUG,
            "load_config: created switch trigger for dimmer %s",
            dimmers[i].name);
      } else {
        syslog(LOG_INFO,
            "load_config: failed to create switch trigger for dimmer %s",
            dimmers[i].name);
      }
      if (json_get_trigger(stepga, TRIGGER_STEPDIM, TARGET_DIMMER, i)) {
        syslog(LOG_DEBUG,
            "load_config: created 4-bit dim trigger for dimmer %s",
            dimmers[i].name);
      } else {
        syslog(LOG_INFO,
            "load_config: failed to create 4-bit dim trigger for dimmer %s",
            dimmers[i].name);
      }
    }
    // get turnonvalue
    struct json_object *tov = json_object_object_get(element,
        "turnonvalue");
    if (tov) {
      dimmers[i].turnon_value = json_object_get_int(tov);
      syslog(LOG_DEBUG,
          "load_config: turnon_value configured for dimmer %s = %d",
          dimmers[i].name, dimmers[i].turnon_value);
      dimmers[i].last_value = dimmers[i].turnon_value;
    } else {
      dimmers[i].turnon_value = 0;
      dimmers[i].last_value = 0;
    }

    // get fading
    struct json_object *fading = json_object_object_get(element, "fading");
    dimmers[i].fading = fading ? json_object_get_double(fading) : 0.0;

    struct json_object *dimtime = json_object_object_get(element, "dimtime");
    dimmers[i].dimtime = dimtime ? json_object_get_double(dimtime) : 0.0;

    struct json_object *fading_ga = json_object_object_get(element,
        "fadingga");
    if (fading_ga) {
      if (json_get_trigger(element, TRIGGER_FADING, TARGET_DIMMER, i)) {
        syslog(LOG_DEBUG,
            "load_config: created fading trigger for dimmer %s",
            dimmers[i].name);
      }
    }

  }

  /*
   * scenes
   */

  in_data = json_object_object_get(config, "scenes");
  scene_num = json_object_array_length(in_data);
  syslog(LOG_DEBUG, "load_config: trying to import %d scene(s) ", scene_num);

  scenes = (cue_t*) calloc(scene_num, sizeof(*scenes));
  if (scenes == NULL) {
    syslog(LOG_ERR,
        "load_config: could not allocate memory for scene definitions");
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < scene_num; i++) { // read all
    struct json_object *scene = json_object_array_get_idx(in_data, i);

    // get name & create

    if (json_get_cue(&(scenes[i]), scene, i, TARGET_SCENE)) { // we still need the go-trigger
      struct json_object *triggers = json_object_object_get(scene,
          "trigger");
      if (!triggers) {
        syslog(LOG_INFO, "load_config: scene %s has no trigger defined",
            scenes[i].name);
        continue;
      }
      struct json_object *go = json_object_object_get(triggers, "go");
      if (json_get_trigger(go, TRIGGER_GO, TARGET_SCENE, i)) {
        syslog(LOG_DEBUG,
            "load_config: created go trigger for scene %s",
            scenes[i].name);
      } else {
        syslog(LOG_INFO,
            "load_config: failed to created trigger for scene %s",
            scenes[i].name);
      }
      struct json_object *dim = json_object_object_get(triggers, "dim");
      if (json_get_trigger(dim, TRIGGER_DIM, TARGET_SCENE, i)) {
        syslog(LOG_DEBUG,
            "load_config: created dim trigger for scene %s",
            scenes[i].name);
      }

    }
  }
  /*
   * cuelists
   */

  struct json_object *json_cuelists = json_object_object_get(config,
      "cuelists");
  cuelist_num = json_object_array_length(json_cuelists);
  syslog(LOG_DEBUG, "load_config: trying to import %d cuelist(s)",
      cuelist_num);

  cuelists = (cuelist_t*) calloc(cuelist_num, sizeof(*cuelists));
  if (cuelists == NULL) {
    syslog(LOG_ERR,
        "load_config: could not allocate memory for cuelist definitions");
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < cuelist_num; i++) { // read all
    struct json_object *cuelist = json_object_array_get_idx(json_cuelists,
        i);

    struct json_object *name = json_object_object_get(cuelist, "name");
    strdcopy(&cuelists[i].name,
        (name) ? json_object_get_string(name) : "unknown");

    struct json_object *roh = json_object_object_get(cuelist,
        "release_on_halt");
    cuelists[i].release_on_halt =
        roh ? json_object_get_boolean(roh) : false;

    struct json_object *pog = json_object_object_get(cuelist,
        "proceed_on_go");
    cuelists[i].proceed_on_go = pog ? json_object_get_boolean(pog) : true;

    struct json_object *cues = json_object_object_get(cuelist, "cues");
    if (!cues) {
      syslog(LOG_INFO,
          "load_config: cuelist %s does not contain cues, skipped",
          cuelists[i].name);
      continue;
    }

    cuelists[i].dim = 255; // default is cuelist full on

    cuelists[i].cue_num = json_object_array_length(cues);
    cuelists[i].cues = (cue_t*) calloc(cuelists[i].cue_num,
        sizeof(*cuelists[i].cues));
    if (cuelists[i].cues == NULL) {
      syslog(LOG_ERR,
          "load_config: could not allocate memory for cue definitions in cuelist %s",
          cuelists[i].name);
      exit(EXIT_FAILURE);
    }

    for (j = 0; j < cuelists[i].cue_num; j++) { // read all
      struct json_object *cue = json_object_array_get_idx(cues, j);
      json_get_cue(&cuelists[i].cues[j], cue, j, TARGET_CUE);
    }

    for (j = 0; j < cuelists[i].cue_num; j++) { // make links
      if (cuelists[i].cues[j].is_link) {
        size_t k;
        bool found = false;
        for (k = 0; k < cuelists[i].cue_num; k++) {
          if ((strcmp(cuelists[i].cues[j].name,
              cuelists[i].cues[k].name) == 0)
              && (!cuelists[i].cues[k].is_link)) {
            found = true;
            break;
          }
        }
                if (found) {
                    cuelists[i].cues[j].cue_link = k;
                    syslog(LOG_DEBUG,
            "load_config: making link from cue %d to cue %d in cuelist %s",
            j, k, cuelists[i].name);
                } else {
                    syslog(LOG_WARNING,
            "load_config: making link from cue %d in cuelist %s failed (link not found)",
            j, cuelists[i].name);                
                }
      }
    }

    for (j = 0; j < cuelists[i].cue_num; j++) { // make links
      syslog(LOG_DEBUG, "load_config: cue %d %s, %d, link %d", j, cuelists[i].cues[j].name, cuelists[i].cues[j].is_link, cuelists[i].cues[j].cue_link);
    }

    struct json_object *triggers = json_object_object_get(cuelist,
        "trigger");
    if (!triggers) {
      syslog(LOG_INFO,
          "load_config: skipping cuelist %s, no triggers defined",
          cuelists[i].name);
      continue;
    }

    if (json_get_trigger(json_object_object_get(triggers, "go"), TRIGGER_GO,
    TARGET_CUELIST, i)) {
      syslog(LOG_DEBUG, "load_config: created go trigger for cuelist %s",
          cuelists[i].name);
    } else {
      syslog(LOG_INFO,
          "load_config: failed to create go trigger for cuelists %s",
          cuelists[i].name);
    }
    if (json_get_trigger(json_object_object_get(triggers, "halt"),
    TRIGGER_HALT,
    TARGET_CUELIST, i)) {
      syslog(LOG_DEBUG,
          "load_config: created halt trigger for cuelist %s",
          cuelists[i].name);
    } else {
      syslog(LOG_INFO,
          "load_config: failed to create halt trigger for cuelist %s",
          cuelists[i].name);
    }
    if (json_get_trigger(json_object_object_get(triggers, "direct"),
    TRIGGER_DIRECT, TARGET_CUELIST, i)) {
      syslog(LOG_DEBUG,
          "load_config: created direct trigger for cuelist %s",
          cuelists[i].name);
    } else {
      syslog(LOG_INFO,
          "load_config: failed to create direct trigger for cuelist %s",
          cuelists[i].name);
    }

    if (json_get_trigger(json_object_object_get(triggers, "dim"),
    TRIGGER_DIM, TARGET_CUELIST, i)) {
      syslog(LOG_DEBUG, "load_config: created dim trigger for cuelist %s",
          cuelists[i].name);
    }

    if (json_get_trigger(json_object_object_get(triggers, "release"),
    TRIGGER_RELEASE, TARGET_CUELIST, i)) {
      syslog(LOG_DEBUG,
          "load_config: created release trigger for cuelist %s",
          cuelists[i].name);
    } else {
      syslog(LOG_INFO,
          "load_config: failed to create release trigger for cuelist %s",
          cuelists[i].name);
    }

    cuelists[i].cue_running = false;
    cuelists[i].next_cue = 0;
  }

  qsort(triggers, trigger_num, sizeof(*triggers), trigger_compare); // sort triggers

  return;
}

int main(int argc, char **argv) {

  int daemonize = 0;
  int c;
  char pidstr[255];

  while ((c = getopt(argc, argv, "dp:u:c:r:s:")) != -1)
    switch (c) {
    case 'd':
      daemonize = 1;
      break;
    case 'p':
      pidfilename = optarg;
      break;
    case 'u':
      eibd_url = optarg;
      break;
    case 'c':
      conf_file = optarg;
      break;
    case 'r':
      e131_receiver = optarg;
      break;
    case 's':
      e131_sender = optarg;
      break;
    case '?':
      //FIXME: check arguments better, print_usage
      fprintf(stderr, "Unknown option `-%c'.\nUsage: %s %s", optopt,
          argv[0],
          USAGESTRING);
      return 1;
    default:
      abort();
      break;
    }

  signal(SIGHUP, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGQUIT, signal_handler);

  if (!daemonize) {
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog(DAEMON_NAME, LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID,
    LOG_USER);
    syslog(LOG_DEBUG, "main: startup with debug; pidfile: %s, eibd: %s ",
        pidfilename, eibd_url);
  } else {
    setlogmask(LOG_UPTO(LOG_INFO));
    openlog(DAEMON_NAME, LOG_CONS, LOG_USER);
  }
  syslog(LOG_DEBUG, "main: %s %s (build %s), compiled on %s %s with GCC %s",
  DAEMON_NAME,
  DAEMON_VERSION, BUILD, __DATE__, __TIME__, __VERSION__);

  syslog(LOG_INFO, "main: using config-file %s", conf_file);

  pid_t pid, sid;

  if (daemonize) {
    syslog(LOG_INFO, "main: starting daemon");

    pid = fork();
    if (pid < 0) {
      exit(EXIT_FAILURE);
    }
    if (pid > 0) {
      exit(EXIT_SUCCESS);
    }
    umask(0);
    sid = setsid();
    if (sid < 0) {
      exit(EXIT_FAILURE);
    }
    if ((chdir("/")) < 0) {
      exit(EXIT_FAILURE);
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

//FIXME: output errors to stderr, change order
  pidFilehandle = open(pidfilename, O_RDWR | O_CREAT, 0600);
  if (pidFilehandle == -1) {
    syslog(LOG_ERR, "main: could not open pidfile %s, exiting",
        pidfilename);
    fprintf(stderr, "main: could not open pidfile %s, exiting \n",
        pidfilename);
    exit(EXIT_FAILURE);
  }
  if (lockf(pidFilehandle, F_TLOCK, 0) == -1) {
    syslog(LOG_ERR, "main: could not lock pidfile %s, exiting",
        pidfilename);
    fprintf(stderr, "main: could not lock pidfile %s, exiting \n",
        pidfilename);
    exit(EXIT_FAILURE);
  }

  sprintf(pidstr, "%d\n", getpid());
  c = write(pidFilehandle, pidstr, strlen(pidstr));

  init_E131();
  load_config();

//  int dmxthread, knxreceiver, crossfadeprocessor, knxsender, cueprocessor;
  pthread_t threads[NUM_THREADS];

//  dmxthread = 
    pthread_create(&threads[0], NULL, dmx_sender, NULL);
//  knxreceiver = 
    pthread_create(&threads[1], NULL, knx_receiver, NULL);
//  knxsender = 
    pthread_create(&threads[2], NULL, knx_sender, NULL);
//  crossfadeprocessor = 
    pthread_create(&threads[3], NULL, crossfade_processor,
  NULL);
//  cueprocessor = 
    pthread_create(&threads[4], NULL, cue_processor, NULL);

  while (1) {
    sleep(POLLING_INTERVAL * 1000);
  }

// TODO: Free any allocated resources before exiting - we never get here though -> signal handler
  exit(EXIT_SUCCESS);
}

