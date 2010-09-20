#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <math.h>

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

#define FILES_PER_DIR 1000
#define TIMER_T unsigned long long int
/* This needs to be defined because the cache gc will use it as
 * an extern */
char ganesha_exec_path[MAXPATHLEN];

struct hashtable_func {
  void *(*init)(hash_parameter_t);
  int (*get)(void *, hash_buffer_t *, hash_buffer_t **);
  int (*set)(void *, hash_buffer_t *, hash_buffer_t *, hashtable_set_how_t);
  int (*del)(void *, hash_buffer_t *, hash_buffer_t **, hash_buffer_t **);
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
  .index_size = 17,
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


/* GLOBALS */
statistics *stats;
TIMER_T starttime;
TIMER_T endtime;
struct hashtable_func *func_tab;

enum hashtable_t {GANESHA, GLIB};
enum hashtable_t HASHTABLE;

int debug = 0;

TIMER_T get_time(){
  struct timeval t;
  gettimeofday(&t, NULL);
  //  double d = t.tv_sec + (double) t.tv_usec/1000000;
  TIMER_T d = t.tv_sec * pow(10,6) + t.tv_usec;
  return d;
}

/* generates a value to store in the hash table. */
void generate_data(hash_buffer_t **key, hash_buffer_t **val,
		   char *filename, fsal_op_context_t *context, int i) {
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

  *val = malloc(sizeof(hash_buffer_t));
  *key = malloc(sizeof(hash_buffer_t));
  if ((*val)== NULL || (*key)== NULL) {
    LogTest("Couldn't malloc hash_buffer_t");
    exit(1);
  }

  (*val)->pdata = malloc(sizeof(cache_entry_t));
  (*key)->pdata = malloc(sizeof(cache_inode_fsal_data_t));
  if ((*val)->pdata == NULL || (*key)->pdata == NULL) {
    LogTest("Couldn't malloc key/value pair");
    exit(1);
  }

  ((cache_inode_fsal_data_t *) (*key)->pdata)->handle = handle;
  ((cache_inode_fsal_data_t *) (*key)->pdata)->cookie = i;

  ((cache_entry_t *) (*val)->pdata)->internal_md.type = REGULAR_FILE;
  ((cache_entry_t *) (*val)->pdata)->object.file.handle = handle;
  ((cache_entry_t *) (*val)->pdata)->object.file.pentry_content = (void *)filename;
  ((cache_entry_t *) (*val)->pdata)->object.file.open_fd.fileno = i;

  (*val)->len = sizeof(cache_entry_t);
  (*key)->len = sizeof(cache_inode_fsal_data_t);
}

void free_hash_key(hash_buffer_t *key) {
  free(key->pdata);
  free(key);
}

void free_hash_val(hash_buffer_t *val) {
  free( ((cache_entry_t *) (val)->pdata)->object.file.pentry_content );
  free(val->pdata);
  free(val);
}

void help_and_quit(char *progname) {
    LogTest( "Usage:  \n%s\t--implementation=[GANESHA|GLIB]"
	     " \n\t\t--keys=[number of keys to test] \n\t\t--configfile=[configuration file]"
	     " \n\t\t--time=[seconds to run test] \n\t\t--operation=[SET|GET|DEL]"
	     " \n\t\t--testdirectory=[path to a dir with test files]\n", progname);
    exit(1);
}
int create_filename(char *testfile, char *path, int dir, int file) {
  int len = strlen(path);
  strcpy(testfile, path);

  /* Add numerically named files and directories to end of export path */
  /* This is where it is assumed test files will reside. */
  len += snprintf(testfile+len, MAXPATHLEN - len, "%d/%d", dir, file);
  testfile[len] = 0;
  return len;
}

void store(void *ht, fsal_op_context_t *context, fsal_path_t exportpath_fsal, int dir, int file) {
  int rc;
  hash_buffer_t *val, *key;
  char *testfile = malloc(sizeof(char) * MAXPATHLEN);
  int testfile_len;
  TIMER_T time;

  testfile_len = create_filename(testfile, exportpath_fsal.path, dir, file);
  generate_data(&key, &val, testfile, context, dir*FILES_PER_DIR+file);
  starttime = get_time();
  if (debug) {
    LogTest("SET %d %d: %p %p", dir, file, key, val);
    LogTest("filename: %p", testfile);
  }
  rc = func_tab->set(ht, key, val, HASHTABLE_SET_HOW_SET_OVERWRITE);
  
  endtime = get_time();
  time = endtime - starttime;     

  stats->tot_num_sets++;
  stats->tot_set_time += time;
  stats->tot_set_time += time;
  if (time > stats->get_time_high)
    stats->set_time_high = time;
  if (time < stats->get_time_low)
    stats->set_time_low = time;

  if (rc != HASHTABLE_SUCCESS) {
    LogTest("Failed to add a key/value pair to hashtable!");
    exit(1);
  }
}

void retrieve(void *ht, fsal_op_context_t *context, fsal_path_t exportpath_fsal, int dir, int file) {
  int rc;
  hash_buffer_t *val, *oldval, *key;
  char *testfile = malloc(sizeof(char) * MAXPATHLEN);
  int testfile_len;
  TIMER_T time;
  int counter;
  testfile_len = create_filename(testfile, exportpath_fsal.path, dir, file);
  generate_data(&key, &val, testfile, context, dir*FILES_PER_DIR+file);
  oldval = NULL;

  starttime = get_time();
  rc = func_tab->get(ht, key, &oldval);
  endtime = get_time();                                                                                                                                                        
  time = endtime - starttime;     

  stats->tot_num_gets++;
  stats->tot_get_time += time;
  if (time > stats->get_time_high)
    stats->get_time_high = time;
  if (time < stats->get_time_low)
    stats->get_time_low = time;

  if (rc != HASHTABLE_SUCCESS) {
    LogTest("Failed to retrieve a value from hashtable during retrieve! %s", testfile);
    exit(1);
  }
  if (debug) {
    LogTest("GOT %d %d: %p %p ---> %p", dir, file, key, val, oldval);

    /* Check that the retrieved value is correct */
    if (((cache_entry_t *) oldval->pdata)->internal_md.type != REGULAR_FILE) {
      LogTest("(get)Retrieved incorrect file type for %d %d", dir, file);
      exit(1);
    }
    
    for(counter=0;
	counter < ((cache_entry_t *) (oldval)->pdata)->object.file.handle.handle.handle_size;
	counter++)
      {
	if (((cache_entry_t *) (oldval)->pdata)->object.file.handle.handle.f_handle[counter] !=
	    ((cache_entry_t *) (val)->pdata)->object.file.handle.handle.f_handle[counter]) 
	  {
	    LogTest("Retrieved incorrect handle for %d %d", dir, file);
	    exit(1);
	  }
      }
    
    if (strncmp(((cache_entry_t *) (oldval)->pdata)->object.file.pentry_content,
		testfile, testfile_len) != 0) {
      LogTest("Retrieved incorrect filename for %d %d", dir, file);    
      exit(1);
    }
    
    LogTest("first: %s", ((cache_entry_t *) (oldval)->pdata)->object.file.pentry_content);
    LogTest("second: %s", testfile);
    LogTest("filename: %p", ((cache_entry_t *) (oldval)->pdata)->object.file.pentry_content);
    if ( ((cache_entry_t *) (oldval)->pdata)->object.file.open_fd.fileno
	 != dir*FILES_PER_DIR+file) {
      LogTest("Retrieved incorrect file number for %d %d", dir, file);    
      exit(1);
    }
  }

  free_hash_val(val);
  free_hash_key(key);
  /* THERE WILL BE A MEMORY LEAK OR CONFLICT HERE DEPENDING ON THE HASHTABLE IMPLEMENTATION */
  /* GANESHA ONLY RETURNS THE PDATA, NOT THE HASH_VALUE_T DATA STRUCTURE */
  if (HASHTABLE == GANESHA)
    free(oldval);
}

void retrieve_no_stats(void *ht, fsal_op_context_t *context, fsal_path_t exportpath_fsal, int dir, int file) {
  int rc;
  hash_buffer_t *val, *oldval, *key;
  char *testfile = malloc(sizeof(char) * MAXPATHLEN);
  int testfile_len;
  int counter;

  testfile_len = create_filename(testfile, exportpath_fsal.path, dir, file);
  generate_data(&key, &val, testfile, context, dir*FILES_PER_DIR+file);
  oldval = NULL;

  rc = func_tab->get(ht, key, &oldval);

  if (rc != HASHTABLE_SUCCESS) {
    LogTest("Failed to retrieve a value from hashtable during retrieve! %s", testfile);
    exit(1);
  }

  if (debug) {
    LogTest("GOT-NS %d %d: %p %p ---> %p", dir, file, key, val, oldval);

    /* Check that the retrieved value is correct */
    if (((cache_entry_t *) oldval->pdata)->internal_md.type != REGULAR_FILE) {
      LogTest("(nostat)Retrieved incorrect file type for %d %d", dir, file);
      exit(1);
    }
    
    for(counter=0;
	counter < ((cache_entry_t *) (oldval)->pdata)->object.file.handle.handle.handle_size;
	counter++)
      {
	if (((cache_entry_t *) (oldval)->pdata)->object.file.handle.handle.f_handle[counter] !=
	    ((cache_entry_t *) (val)->pdata)->object.file.handle.handle.f_handle[counter]) 
	  {
	    LogTest("Retrieved incorrect handle for %d %d", dir, file);
	    exit(1);
	  }
      }
    
    if (strncmp(((cache_entry_t *) (oldval)->pdata)->object.file.pentry_content,
		testfile, testfile_len) != 0) {
      LogTest("Retrieved incorrect filename for %d %d", dir, file);
      LogTest("filename: %p", ((cache_entry_t *) (oldval)->pdata)->object.file.pentry_content);
      exit(1);
    }
    
    
    if ( ((cache_entry_t *) (oldval)->pdata)->object.file.open_fd.fileno
	 != dir*FILES_PER_DIR+file) {
      LogTest("Retrieved incorrect file number for %d %d", dir, file);    
      exit(1);
    }
  }
  
  free_hash_val(val);
  free_hash_key(key);
  /* THERE WILL BE A MEMORY LEAK OR CONFLICT HERE DEPENDING ON THE HASHTABLE IMPLEMENTATION */
  /* GANESHA ONLY RETURNS THE PDATA, NOT THE HASH_VALUE_T DATA STRUCTURE */
  if (HASHTABLE == GANESHA)
    free(oldval);
}

void del(void *ht, fsal_op_context_t *context, fsal_path_t exportpath_fsal, int dir, int file) {
  int rc;
  hash_buffer_t *val, *oldval, *key, *oldkey;
  char *testfile = malloc(sizeof(char) * MAXPATHLEN);
  int testfile_len;
  TIMER_T time;
  int counter;

  testfile_len = create_filename(testfile, exportpath_fsal.path, dir, file);
  generate_data(&key, &val, testfile, context, dir*FILES_PER_DIR+file);
  oldval = oldkey = NULL;

  starttime = get_time();                                                                                                                                                      
  rc = func_tab->del(ht, key, &oldkey, &oldval);
  endtime = get_time();                                                                                                                                                        
  time = endtime - starttime;     

  stats->tot_num_dels++;
  stats->tot_del_time += time;
  if (time > stats->del_time_high)
    stats->del_time_high = time;
  if (time < stats->del_time_low)
    stats->del_time_low = time;

  if (rc != HASHTABLE_SUCCESS) {
    LogTest("Failed to remove a key/value pair from hashtable! %s", testfile);
    exit(1);
  }

  if (debug) {
    /* Check that the retrieved value is correct */
    if (((cache_entry_t *) oldval->pdata)->internal_md.type != REGULAR_FILE) {
      LogTest("(del)Retrieved incorrect file type for %d %d", dir, file);
      exit(1);
    }
    
    for(counter=0;
	counter < ((cache_entry_t *) (oldval)->pdata)->object.file.handle.handle.handle_size;
	counter++)
      {
	if (((cache_entry_t *) (oldval)->pdata)->object.file.handle.handle.f_handle[counter] !=
	    ((cache_entry_t *) (val)->pdata)->object.file.handle.handle.f_handle[counter]) 
	  {
	    LogTest("Retrieved incorrect handle for %d %d", dir, file);
	    exit(1);
	  }
      }
    
    if (strncmp(((cache_entry_t *) (oldval)->pdata)->object.file.pentry_content,
		testfile, testfile_len) != 0) {
      LogTest("Retrieved incorrect filename for %d %d", dir, file);    
      exit(1);
    }
    
    if ( ((cache_entry_t *) (oldval)->pdata)->object.file.open_fd.fileno
	 != dir*FILES_PER_DIR+file) {
      LogTest("Retrieved incorrect file number for %d %d", dir, file);    
      exit(1);
    }
  }

  /* Free key/value pair that was removed from the hashtable */
  free_hash_key(key);
  free_hash_key(oldkey);
  free_hash_val(val);
  free_hash_val(oldval);
}

int main(int argc, char **argv) {

  int curr_opt;
  int option_index = 0;

  uid_t uid;
  fsal_status_t status;
  int rc = 0;
  int count;
  char *testdir = NULL;
  char *config_filename = NULL;
  config_file_t ganesha_config;
  cache_inode_parameter_t config_params;
  fsal_path_t exportpath_fsal;
  void *ht;
  fsal_op_context_t context;
  fsal_export_context_t fs_export_context;
  int numkeys = -1; /* number of files to cache during benchmark */
  int timetorun = 60; /* seconds */
  nfs_parameter_t nfs_param;
  int random_key;

  /* counters for benchmarking purposes */
  int dirnum;
  int filenum;

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
      {"debug",            required_argument, 0, 'D'},
      {0, 0, 0, 0}
    };
  curr_opt = getopt_long (argc, argv, "i:t:c:k:o:d:",
			  long_options, &option_index);
  while (curr_opt != -1) {
    switch (curr_opt) {
    case 'i':
      if (strncmp(optarg, "GANESHA", 7) == 0) {
	func_tab = &ganesha_func;
	HASHTABLE = GANESHA;
      }
      else if (strncmp(optarg, "GLIB", 7) == 0) {
	func_tab = &glib_func;
	HASHTABLE = GLIB;
      }
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
    case 'D':
      debug = 1;
      break;
    default:
      help_and_quit(argv[0]);
    }
    curr_opt = getopt_long (argc, argv, "i:t:c:k:o:",
			    long_options, &option_index);
  }
  if (config_filename == NULL || testdir == NULL) {
    LogTest("Include configuration filename and test directory location.");
    help_and_quit(argv[0]);
  }
  if (numkeys == -1) {
    LogTest("Include number of keys to cache.");
    help_and_quit(argv[0]);
  }

  /* Load functions and constants for the FSAL we are currently using.*/
  FSAL_LoadFunctions();
  FSAL_LoadConsts();

  /* read hash table parameters from config file for initializing hashtables. */
  config_params.hparam.index_size = -1;
  config_params.hparam.alphabet_length = -1;
  config_params.hparam.nb_node_prealloc = -1;
  /* Parse config file */
  if((ganesha_config = config_ParseFile(config_filename)) == NULL)
    {
      LogTest("Error parsing config file %s\n", config_filename);
      exit(1);
    }
  cache_inode_read_conf_hash_parameter(ganesha_config, &config_params);
  if (config_params.hparam.index_size != -1)
    parameters.index_size = config_params.hparam.index_size;
  LogTest("INDEX SIZE: %d", parameters.index_size);
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
  if(FSAL_IS_ERROR(status))
    {
      LogTest( "Couldn't build export context for %s",exportpath_fsal.path);
      exit(1);
    }
  if(FSAL_IS_ERROR(status = FSAL_InitClientContext(&context)))
   {
      LogTest( "Couldn't get the context for FSAL super user");
      exit(1);
    }
  ///////////////////////////////////////////////////////////////////////////

  /* Initialize testing framework */
  stats = new_statistics();
  ht = func_tab->init(parameters);

  ///////////////////////////////////////////////////////////////////////////
  /* run benchmark ....... */
  for(count=0; count < 5; count++) {
    fprintf(stderr, "b%d\n", count);
    for(dirnum=0; dirnum-1 < (numkeys/FILES_PER_DIR); dirnum++) 
      for(filenum=0; filenum < FILES_PER_DIR && (dirnum*FILES_PER_DIR+filenum < numkeys); filenum++)
	store(ht, &context, exportpath_fsal, dirnum,filenum);
    for(dirnum=0; dirnum-1 < (numkeys/FILES_PER_DIR); dirnum++)
      for(filenum=0; filenum < FILES_PER_DIR && (dirnum*FILES_PER_DIR+filenum < numkeys); filenum++)
	retrieve_no_stats(ht, &context, exportpath_fsal, dirnum,filenum);

    /* randomly retrieve pairs */
    srand( (unsigned)time(NULL) );
    for(dirnum=0; dirnum < numkeys*20; dirnum++) {
      random_key = rand() % numkeys;
      retrieve(ht, &context, exportpath_fsal, random_key/FILES_PER_DIR, random_key%FILES_PER_DIR);
    }
    
    for(dirnum=0; dirnum-1 < (numkeys/FILES_PER_DIR); dirnum++)
      for(filenum=0; filenum < FILES_PER_DIR && (dirnum*FILES_PER_DIR+filenum < numkeys); filenum++)
	del(ht, &context, exportpath_fsal, dirnum,filenum);
    fprintf(stderr, "a%d\n", count);
  }
  fprintf(stderr, "aaaaaaaaaaaaaaaaaaaaaa");
  /* summarize statistics */
  print_statistics(stats, numkeys);
  fprintf(stderr, "bbbbbbbbbbbbbbbbbbbbbb");
  return 0;
}
