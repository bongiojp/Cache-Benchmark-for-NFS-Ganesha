#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>

/* cacheing algorithms */
#include "ganesha_ht.h"
#include "glib_ht.h"

/* data structure for recording benchmark statistics */
#include "benchstats.h"

/* general ganesha headers */
#include "cache_inode.h"
#include "fsal_types.h"
#include "log_macros.h"
#include "fsal_glue.h"
#include "nfs_core.h"
#include "fsal.h"
#include "../MainNFSD/nfs_init.h"

#define FILES_PER_DIR 100

/* This needs to be defined because the cache gc will use it as
 * an extern */
char ganesha_exec_path[MAXPATHLEN];

struct hashtable_func {
  void *(*init)(hash_parameter_t);
  int (*get)(void *, hash_buffer_t *, hash_buffer_t **);
  int (*set)(void *, hash_buffer_t *, hash_buffer_t *, hashtable_set_how_t);
  int (*del)(void *, hash_buffer_t *, hash_buffer_t *, hash_buffer_t *);
  unsigned int (*getsize)(void *);
  void (*dest)(void);
};

/* functions from ganesha_ht.h */
struct hashtable_func ganesha_func = {
  .init = gan_init,
  .get = gan_get,
  .set = gan_set,
  .del = gan_del,
  .getsize = gan_getsize,
  .dest = NULL
};

/* functions from glib_ht.h */
struct hashtable_func glib_func = {
  .init = glib_init,
  .get = glib_get,
  .set = glib_set,
  .del = glib_del,
  .getsize = glib_getsize,
  .dest = NULL
};

/* From MainNFSD/nfs_init.c */
hash_parameter_t parameters = 
{
  .index_size = 500,
  .alphabet_length = 10, 
  .nb_node_prealloc = 100, 
  .hash_func_key = cache_inode_fsal_hash_func,
  .hash_func_rbt = cache_inode_fsal_rbt_func,
  .compare_key = cache_inode_compare_key_fsal,
  .key_to_str = NULL, /*int display_cache(hash_buffer_t * pbuff, char *str)  { return 0;  }*/
  .val_to_str = NULL  /*int display_cache(hash_buffer_t * pbuff, char *str)  { return 0;  }*/
};

/* Needed for reading the configuration file so we can initialize the fsal
 * so we can use the fsal for getting real file handles. */
nfs_start_info_t my_nfs_start_info = {
  .flush_datacache_mode = FALSE,
  .dump_default_config = FALSE,
  .nb_flush_threads = 1,
  .flush_behaviour = CACHE_CONTENT_FLUSH_AND_DELETE,
  .lw_mark_trigger = FALSE
};


void generate_data(hash_buffer_t **data, char *filename, fsal_op_context_t *context) {
  fsal_handle_t handle;
  fsal_path_t fsalpath;
  fsal_attrib_list_t attribs;
  fsal_status_t status;

  if((FSAL_IS_ERROR(status = FSAL_str2path(filename, FSAL_MAX_PATH_LEN, &fsalpath))))
    {
      LogTest("str2path failed for %s", filename);
      exit(1);
    }

  if((FSAL_IS_ERROR(status = FSAL_lookupPath(&fsalpath, context, &handle, &attribs))))
    {
      LogTest("Lookup failed for %s", fsalpath.path);
      exit(1);
    }

  *data = malloc(sizeof(hash_buffer_t));
  (*data)->pdata = malloc(sizeof(cache_entry_t));
  //  fsdata.cookie = 0;
  //  fsdata.handle = root_handle
}

void free_data(hash_buffer_t *data) {
  free(data->pdata);
  free(data);
}

void help_and_quit(char *progname) {
    LogTest( "Usage:  \n%s\t--implementation=[GANESHA|GLIB]"
	     " \n\t\t--keys=[number of keys to test] \n\t\t--configfile=[configuration file]"
	     " \n\t\t--time=[seconds to run test] \n\t\t--operation=[SET|GET|DEL]"
	     " \n\t\t--testdirectory=[path to a dir with test files]\n", progname);
    exit(1);
}

double get_time(){
  struct timeval t;
  gettimeofday(&t, NULL);
  double d = t.tv_sec + (double) t.tv_usec/1000000;
  //CLOCKS_PER_SEC                                                                                                                                                                 
  return d;
}

int main(int argc, char **argv) {

  int curr_opt;
  int option_index = 0;

  struct hashtable_func *func_tab;
  uid_t uid;
  char *testfile;
  fsal_status_t status;
  int rc = 0;
  char *testdir = NULL;
  char *config_filename = NULL;
  config_file_t ganesha_config;
  cache_inode_parameter_t config_params;
  void *ht;
  fsal_op_context_t context;
  fsal_export_context_t fs_export_context;
  fsal_path_t exportpath_fsal;
  int numkeys = -1; /* number of files to cache during benchmark */
  int timetorun = 60; /* seconds */
  nfs_parameter_t nfs_param;

  /* counters for benchmarking purposes */
  int dirnum;
  int filenum;

  /* statistics and timing */
  statistics *stats;
  double starttime;// = get_time();
  double endtime;

  /* Initialize logging system */
  SetDefaultLogging("TEST");
  SetNamePgm("cache benchmarking");
  SetNameFunction("main");
  InitLogging();

  static struct option long_options[] =
    {
      {"implementation",     required_argument, 0, 'i'},
      {"time",               required_argument, 0, 't'},
      {"keys",               required_argument, 0, 'k'},
      {"configfile",         required_argument, 0, 'c'},
      {"operation",          required_argument, 0, 'o'},
      {"testdirectory",      required_argument, 0, 'd'},
      {0, 0, 0, 0}
    };
  curr_opt = getopt_long (argc, argv, "i:t:c:k:o:d:",
			  long_options, &option_index);
  while (curr_opt != -1) {
    switch (curr_opt) {
    case 'i':
      if (strncmp(optarg, "GANESHA", 7) == 0)
	func_tab = &ganesha_func;
      else if (strncmp(optarg, "GLIB", 7) == 0)
	func_tab = &glib_func;
      else {
	LogTest( "The hashtable must be either GANESHA or GLIB.\n");
	exit(1);
      }
      break;
    case 't':
      timetorun = atoi(optarg);
      break;
    case 'c':
      config_filename = optarg;
      break;
    case 'k':
      numkeys = atoi(optarg);
      break;
    case 'o':
      break;
    case 'd':
      testdir = optarg;
      break;
    default:
      help_and_quit(argv[0]);
    }
    curr_opt = getopt_long (argc, argv, "i:t:c:k:o:",
			    long_options, &option_index);
  }
  if (config_filename == NULL || testdir == NULL)
    help_and_quit(argv[0]);
  if (numkeys == -1)
    help_and_quit(argv[0]);
  LogTest("ff");

  /* Load functions and constants for the FSAL we are currently using.*/
  FSAL_LoadFunctions();
  FSAL_LoadConsts();

  /* read hash table parameters from config file for initializing hashtables. */
  config_params.hparam.index_size = -1;
  config_params.hparam.alphabet_length = -1;
  config_params.hparam.nb_node_prealloc = -1;
  LogTest("gg");
  /* Parse config file */
  if((ganesha_config = config_ParseFile(config_filename)) == NULL)
    {
      LogTest("Error parsing config file %s\n", config_filename);
      exit(1);
    }
  cache_inode_read_conf_hash_parameter(ganesha_config, &config_params);
  if (config_params.hparam.index_size != -1)
    parameters.index_size = config_params.hparam.index_size;
  if (config_params.hparam.alphabet_length != -1)
    parameters.alphabet_length = config_params.hparam.alphabet_length;
  if (config_params.hparam.nb_node_prealloc != -1)
    parameters.nb_node_prealloc = config_params.hparam.nb_node_prealloc;
  /* getting build and client context for file handle lookup */
  /* We are NOT exporting anything here, but some FSAL's may store data
   * in this structure which they require to perform lookups. */

  if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogTest("Memory manager could not be initialized");
      exit(1);
    }

  if(nfs_set_param_default(&nfs_param))
    {
      LogMajor(COMPONENT_INIT, "NFS MAIN: Error setting default parameters.");
      exit(1);
    }

  if(nfs_set_param_from_conf(&nfs_param, &my_nfs_start_info, config_filename))
    {
      LogMajor(COMPONENT_INIT, "NFS MAIN: Error parsing configuration file.");
      exit(1);
    }

  if(FSAL_IS_ERROR(status = FSAL_Init(&nfs_param.fsal_param)))
    {
      /* Failed init */
      LogMajor(COMPONENT_INIT, "NFS_INIT: FSAL library could not be initialized");
      exit(1);
    }

  if((FSAL_IS_ERROR(status = FSAL_str2path(testdir, FSAL_MAX_PATH_LEN, &exportpath_fsal))))
    {
      LogTest("str2path failed for %s", testdir);
      exit(1);
    }
      status = FSAL_BuildExportContext(&fs_export_context, &exportpath_fsal, NULL);
      LogTest("ll");  
  if(FSAL_IS_ERROR(status))
    {
      LogTest( "Couldn't build export context for %s",exportpath_fsal.path);
      exit(1);
    }
      LogTest("ooo");    
  if(FSAL_IS_ERROR(status = FSAL_InitClientContext(&context)))
    {
      LogTest( "Couldn't get the context for FSAL super user");
      exit(1);
    }
      LogTest("pp");  
  ///////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////

  /* Initialize testing framework */
  stats = new_statistics();
  ht = func_tab->init(parameters);

  ///////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////

  /* run benchmark */
  for(dirnum=0; dirnum < (numkeys/FILES_PER_DIR); dirnum++) {
    for(filenum=0; filenum < (numkeys/FILES_PER_DIR); filenum++) {
      starttime = get_time();
      endtime = get_time();
      endtime - starttime;
    }
  }

  hash_buffer_t *data;
  generate_data(&data, "./testdata/1", &context);
  free_data(data);

  return 0;
}
