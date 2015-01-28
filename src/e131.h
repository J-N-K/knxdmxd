/*
 * e131.h
 *
 *  Created on: 22.05.2013
 *      Author: jan
 */

#ifndef __E131_H__
#define __E131_H__

#define E131_MCAST_PORT 5568

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>


typedef struct {
  u_int16_t preamble_size, postamble_size;
  u_int8_t packet_ident[12];
  u_int16_t flags_length;
  u_int32_t vector;
  uuid_t CID;
}__attribute__ ((packed)) E131_RL_t;

typedef struct {
  u_int16_t flags_length;
  u_int32_t vector;
  char source_name[64];
  u_int8_t priority;
  u_int16_t reserved;
  u_int8_t sequence_number, options;
  u_int16_t universe;
}__attribute__ ((packed)) E131_FL_t;

typedef struct {
  u_int16_t flags_length;
  u_int8_t vector, address_type;
  u_int16_t first_address, address_increment, property_value_count;
  u_int8_t property_values[513];
}__attribute__ ((packed)) E131_DMP_t;

typedef struct {
  E131_RL_t RL;
  E131_FL_t FL;
  E131_DMP_t DMP;
}__attribute__ ((packed)) E131_packet_t;

#endif /* E131_H_ */
