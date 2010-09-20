#ifndef _BENCHSTATS_H_
#define _BENCHSTATS_H_

#include "log_macros.h"
#define TIMER_T unsigned long long int

typedef struct stat_t {
  int tot_num_gets;
  TIMER_T tot_get_time;
  TIMER_T get_time_high;/* longest recorded time */
  TIMER_T get_time_low; /* shortest recorded time */

  int tot_num_sets;
  TIMER_T tot_set_time;
  TIMER_T set_time_high;/* longest recorded time */
  TIMER_T set_time_low; /* shortest recorded time */

  int tot_num_dels;
  TIMER_T tot_del_time;
  TIMER_T del_time_high;/* longest recorded time */
  TIMER_T del_time_low; /* shortest recorded time */

  int tot_num_getsizes;
  TIMER_T tot_getsize_time;
  TIMER_T getsize_time_high;/* longest recorded time */
  TIMER_T getsize_time_low; /* shortest recorded time */
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
  TIMER_T thous = 1000;

  LogTest("Total Number of Keys in Table at Once: %d", numkeys);
  LogTest("Operation: SET");
  LogTest("\tTotal Count: %d", st->tot_num_sets);

  LogTest("\tTotal Time: %llu us", st->tot_set_time);
  if (st->tot_num_sets == 0)
    LogTest("\tAverage Time: %d us", 0);
  else
    LogTest("\tAverage Time: %f us", (double)st->tot_set_time/st->tot_num_sets);
  LogTest("\tMaximum Time: %llu us", st->set_time_high);
  LogTest("\tMinimum Time: %llu us", st->set_time_low);

  LogTest("Operation: GET");
  LogTest("\tTotal Count: %d", st->tot_num_gets);
  LogTest("\tTotal Time: %llu us", st->tot_get_time);
  if (st->tot_num_gets == 0)
    LogTest("\tAverage Time: %d us", 0);
  else
    LogTest("\tAverage Time: %f us", (double)st->tot_get_time/st->tot_num_gets);
  LogTest("\tMaximum Time: %llu us", st->get_time_high);
  LogTest("\tMinimum Time: %llu us", st->get_time_low);

  LogTest("Operation: DEL");
  LogTest("\tTotal Count: %d", st->tot_num_dels);
  LogTest("\tTotal Time: %llu us", st->tot_del_time);
  if (st->tot_num_dels == 0)
    LogTest("\tAverage Time: %d us", 0);
  else
    LogTest("\tAverage Time: %f us", (double)st->tot_del_time/st->tot_num_dels);
  LogTest("\tMaximum Time: %llu us", st->del_time_high);
  LogTest("\tMinimum Time: %llu us", st->del_time_low);

  LogTest("Operation: GETSIZE");
  LogTest("\tTotal Count: %d", st->tot_num_getsizes);
  LogTest("\tTotal Time: %f ms", (double)st->tot_getsize_time/thous);
  if (st->tot_num_getsizes == 0)
    LogTest("\tAverage Time: %d us", 0);
  else
    LogTest("\tAverage Time: %f us", (double)st->tot_getsize_time/st->tot_num_getsizes);
  LogTest("\tMaximum Time: %llu us", st->getsize_time_high);
  LogTest("\tMinimum Time: %llu us", st->getsize_time_low);

}

#endif /*  _BENCHSTATS_H_ */
