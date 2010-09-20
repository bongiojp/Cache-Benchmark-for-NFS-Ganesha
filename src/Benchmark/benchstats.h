#ifndef _BENCHSTATS_H_
#define _BENCHSTATS_H_

#include "log_macros.h"

typedef struct stat_t {
  int tot_num_gets;
  double tot_get_time;
  double get_time_high;/* longest recorded time */
  double get_time_low; /* shortest recorded time */

  int tot_num_sets;
  double tot_set_time;
  double set_time_high;/* longest recorded time */
  double set_time_low; /* shortest recorded time */

  int tot_num_dels;
  double tot_del_time;
  double del_time_high;/* longest recorded time */
  double del_time_low; /* shortest recorded time */

  int tot_num_getsizes;
  double tot_getsize_time;
  double getsize_time_high;/* longest recorded time */
  double getsize_time_low; /* shortest recorded time */
} statistics;

statistics *new_statistics() {
  statistics *newstats = calloc(1, sizeof(statistics));
  if (newstats == NULL) {
    LogTest("Couldn't calloc memory for statistics.");
    exit(1);
  }
  return newstats;
}

void print_statistics(statistics *st, int numkeys) {
  LogTest("Total Number of Keys in Table at Once: %d", numkeys);

  LogTest("Operation: SET");
  LogTest("\tTotal Count: %d", st->tot_num_sets);
  LogTest("\tTotal Time: %f s", st->tot_set_time);
  LogTest("\tAverage Time: %f ms", (st->tot_set_time/st->tot_num_sets)*1000.0);
  LogTest("\tMaximum Time: %f ms", (st->set_time_high)*1000.0);
  LogTest("\tMinimum Time: %f ms", (st->set_time_low)*1000.0);

  LogTest("Operation: GET");
  LogTest("\tTotal Count: %d", st->tot_num_gets);
  LogTest("\tTotal Time: %f s", st->tot_get_time);
  LogTest("\tAverage Time: %f ms", (st->tot_get_time/st->tot_num_gets)*1000.0);
  LogTest("\tMaximum Time: %f ms", (st->get_time_high)*1000.0);
  LogTest("\tMinimum Time: %f ms", (st->get_time_low)*1000.0);

  LogTest("Operation: DEL");
  LogTest("\tTotal Count: %d", st->tot_num_dels);
  LogTest("\tTotal Time: %f s", st->tot_del_time);
  LogTest("\tAverage Time: %f ms", (st->tot_del_time/st->tot_num_dels)*1000.0);
  LogTest("\tMaximum Time: %f ms", (st->del_time_high)*1000.0);
  LogTest("\tMinimum Time: %f ms", (st->del_time_low)*1000.0);

  LogTest("Operation: GETSIZE");
  LogTest("\tTotal Count: %d", st->tot_num_getsizes);
  LogTest("\tTotal Time: %f s", st->tot_getsize_time);
  LogTest("\tAverage Time: %f ms", (st->tot_getsize_time/st->tot_num_getsizes)*1000.0);
  LogTest("\tMaximum Time: %f ms", (st->getsize_time_high)*1000.0);
  LogTest("\tMinimum Time: %f ms", (st->getsize_time_low)*1000.0);

}

#endif /*  _BENCHSTATS_H_ */
