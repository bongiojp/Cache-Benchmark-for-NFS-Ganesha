#ifndef _BENCHSTATS_H_
#define _BENCHSTATS_H_

#include "log_macros.h"

typedef struct stat_t {
  int tot_num_gets;
  int tot_get_time;
  int get_time_high;/* longest recorded time */
  int get_time_low; /* shortest recorded time */

  int tot_num_sets;
  int tot_set_time;
  int set_time_high;/* longest recorded time */
  int set_time_low; /* shortest recorded time */

  int tot_num_dels;
  int tot_del_time;
  int del_time_high;/* longest recorded time */
  int del_time_low; /* shortest recorded time */

  int tot_num_getsize;
  int tot_getsize_time;
  int getsize_time_high;/* longest recorded time */
  int getsize_time_low; /* shortest recorded time */
} statistics;

statistics *new_statistics() {
  statistics *newstats = calloc(1, sizeof(statistics));
  if (newstats == NULL) {
    LogMajor(COMPONENT_STDOUT, "Couldn't calloc memory for statistics.");
    exit(1);
  }
  return newstats;
}

#endif /*  _BENCHSTATS_H_ */
