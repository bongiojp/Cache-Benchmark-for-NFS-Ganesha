/* Ganesha hashtable data structures */
#include "rbt_node.h"
#include "rbt_tree.h"
#include "HashData.h"
#include "HashTable.h"

/* hash function and compare function for cache inode data. */
#include "cache_inode.h"
#include "fsal_types.h"

/* GLIB hashtable implemenation */
#include "glib/ghash.h"

#include "benchstats.h"

hash_parameter_t my_hparam;

/* key will be hash_buffer_t ... the hparam that is accepted by ganesha's
 * hashtable hash function is not needed here because we record my_hparam
 * globally. */
guint my_ghash(gconstpointer key) {
  guint x = my_hparam.hash_func_key(&my_hparam, (hash_buffer_t *) key);
  return x;
}

gboolean my_comparator(gconstpointer item1, gconstpointer item2) {
  gboolean result = (my_hparam.compare_key((hash_buffer_t *)item1, (hash_buffer_t *)item2) == 0);
  return result;
}

/* return a hash table */
void *glib_init(hash_parameter_t hparam){
  my_hparam = hparam;
  return g_hash_table_new_full(my_ghash, my_comparator, NULL, NULL);
}

int glib_get(void * ht, hash_buffer_t * buffkey, hash_buffer_t ** buffval){
  gpointer result = g_hash_table_lookup((GHashTable*)ht, (gconstpointer) buffkey);
  (*buffval) = result;
  if (result == NULL)
    return 0;
  else
    return HASHTABLE_SUCCESS;
}

int glib_set(void * ht, hash_buffer_t * buffkey, hash_buffer_t * buffval,
	     hashtable_set_how_t how){
  g_hash_table_insert((GHashTable*)ht, (gpointer)buffkey, (gpointer)buffval);
  return 0; //SUCCESS
}

int glib_del(void * ht, hash_buffer_t * buffkey,
	     hash_buffer_t ** p_usedbuffkey, hash_buffer_t ** p_usedbuffdata){
  int result = g_hash_table_lookup_extended((GHashTable*)ht, (gconstpointer) buffkey,
			       (gpointer)p_usedbuffkey, (gpointer)p_usedbuffdata);
  if (result = FALSE)
    return HASHTABLE_ERROR_NO_SUCH_KEY;

  result = g_hash_table_remove((GHashTable*)ht,(gconstpointer) buffkey);
  if (result == TRUE)
    return HASHTABLE_SUCCESS;
  return !HASHTABLE_SUCCESS;
}

unsigned int glib_getsize(void * ht){
  guint result = g_hash_table_size ((GHashTable *)ht);
  return (unsigned int) result;
}


