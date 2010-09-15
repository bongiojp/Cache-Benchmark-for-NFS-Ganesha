/* Ganesha hashtable implementation */
#include "rbt_node.h"
#include "rbt_tree.h"
#include "HashData.h"
#include "HashTable.h"

#include "cache_inode.h"
#include "fsal_types.h"

#include "benchstats.h"

/*
 * The Ganesha hashtable was modified to be independent
 * the Ganesha project. Also it was modified to only use
 * malloc, rather then using the buddy malloc system.
*/

/* return a hash table */
void *gan_init(hash_parameter_t hparam){
  return (void *)(HashTable_Init(hparam));
}

int gan_get(void * ht, hash_buffer_t * buffkey, hash_buffer_t ** buffval){
  (*buffval) = malloc(sizeof(hash_buffer_t));
  int result = HashTable_Get((hash_table_t *) ht, buffkey, *buffval);
  return result;
}

int gan_set(void * ht, hash_buffer_t * buffkey,
	 hash_buffer_t * buffval, hashtable_set_how_t how){
  return HashTable_Test_And_Set((hash_table_t *) ht, buffkey, buffval, how);
}

int gan_del(void * ht, hash_buffer_t * buffkey,
	   hash_buffer_t * p_usedbuffkey, hash_buffer_t * p_usedbuffdata){
  return HashTable_Del((hash_table_t *) ht, buffkey, p_usedbuffkey, p_usedbuffdata);
}

unsigned int gan_getsize(void * ht){
  return HashTable_GetSize((hash_table_t *) ht);
}


