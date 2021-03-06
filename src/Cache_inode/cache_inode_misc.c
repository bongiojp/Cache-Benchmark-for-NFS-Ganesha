/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 *
 * \File    cache_inode_misc.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:51 $
 * \version $Revision: 1.63 $
 * \brief   Some routines for management of the cache_inode layer, shared by other calls. 
 *
 * HashTable.c : Some routines for management of the cache_inode layer, shared by other calls. 
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#ifdef _USE_PROXY
void cache_inode_print_srvhandle(char *comment, cache_entry_t * pentry);
#endif

/**
 *
 * cache_inode_compare_key_fsal: Compares two keys used in cache inode.
 *
 * Compare two keys used in cache inode. These keys are basically made from FSAL 
 * related information. 
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 * @return 0 if keys are the same, 1 otherwise
 * 
 * @see FSAL_handlecmp 
 *
 */
int cache_inode_compare_key_fsal(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  fsal_status_t status;
  cache_inode_fsal_data_t *pfsdata1 = NULL;
  cache_inode_fsal_data_t *pfsdata2 = NULL;

  /* Test if one of teh entries are NULL */
  if(buff1->pdata == NULL)
    return (buff2->pdata == NULL) ? 0 : 1;
  else
    {
      if(buff2->pdata == NULL)
        return -1;              /* left member is the greater one */
      else
        {
          int rc;
          pfsdata1 = (cache_inode_fsal_data_t *) (buff1->pdata);
          pfsdata2 = (cache_inode_fsal_data_t *) (buff2->pdata);

          rc = (!FSAL_handlecmp(&pfsdata1->handle, &pfsdata2->handle, &status)
                && (pfsdata1->cookie == pfsdata2->cookie)) ? 0 : 1;

          return rc;
        }

    }
  /* This line should never be reached */
}                               /* cache_inode_compare_key_fsal */

/**
 *
 * cache_inode_fsaldata_2_key: builds a key from the FSAL data. 
 *
 * Builds a key from the FSAL data. If the key is used for reading and stay local to the function
 * pclient can be NULL (psfsdata in the scope of the current calling routine is used). If the key 
 * must survive after the end of the calling routine, a new key is allocated and ressource in *pclient are used
 *
 * @param pkey [OUT] computed key
 * @param pfsdata [IN] FSAL data to be used to compute the key
 * @param pclient [INOUT] if NULL, pfsdata is used to build the key (that stay local), if not pool_key is used to
 * allocate a new key
 * @return 0 if keys if successfully build, 1 otherwise
 * 
 */
int cache_inode_fsaldata_2_key(hash_buffer_t * pkey, cache_inode_fsal_data_t * pfsdata,
                               cache_inode_client_t * pclient)
{
  cache_inode_fsal_data_t *ppoolfsdata = NULL;

  /* Allocate a new key for storing data, if this a set, not a get
   * in the case of a 'get' key, pclient == NULL and * pfsdata is used */

  if(pclient != NULL)
    {
      GET_PREALLOC(ppoolfsdata,
                   pclient->pool_key,
                   pclient->nb_prealloc, cache_inode_fsal_data_t, next_alloc);
      if(ppoolfsdata == NULL)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                            "Can't allocate a new key from cache pool");
          return 1;
        }

      *ppoolfsdata = *pfsdata;

      pkey->pdata = (caddr_t) ppoolfsdata;
    }
  else
    pkey->pdata = (caddr_t) pfsdata;

  pkey->len = sizeof(cache_inode_fsal_data_t);

  return 0;
}                               /* cache_inode_fsaldata_2_key */

/**
 *
 * cache_inode_release_fsaldata_key: release a fsal key used to access the cache inode 
 * 
 * Release a fsal key used to access the cache inode.
 *
 * @param pkey    [IN]    pointer to the key to be freed
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 *
 * @return nothing (void function)
 *
 */
void cache_inode_release_fsaldata_key(hash_buffer_t * pkey,
                                      cache_inode_client_t * pclient)
{
  cache_inode_fsal_data_t *ppoolfsdata = NULL;

  ppoolfsdata = (cache_inode_fsal_data_t *) pkey->pdata;

  RELEASE_PREALLOC(ppoolfsdata, pclient->pool_key, next_alloc);
}                               /* cache_inode_release_fsaldata_key */

/**
 *
 * cache_inode_new_entry: adds a new entry to the cache_inode.
 *
 * adds a new entry to the cache_inode. These function os used to allocate entries of any kind. Some
 * parameter are meaningless for some types or used for others.
 *
 * @param pfsdata [IN] FSAL data for the entry to be created (used to build the key)
 * @param pfsal_attr [in] attributes for the entry (unused if value == NULL). Used for caching.
 * @param type [IN] type of the entry to be created. 
 * @param link_content [IN] if type == SYMBOLIC_LINK, this is the content of the link. Unused otherwise
 * @param pentry_dir_prev [IN] if type == DIR_CONTINUE, this is the previous entry in the dir_chain. Unused otherwise.
 * @param ht [INOUT] hash table used for the cache.
 * @param pclient [INOUT]ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials for the operation.
 * @param create_flag [IN] a flag which shows if the entry is newly created or not 
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 * 
 */
cache_entry_t *cache_inode_new_entry(cache_inode_fsal_data_t * pfsdata,
                                     fsal_attrib_list_t * pfsal_attr,
                                     cache_inode_file_type_t type,
                                     cache_inode_create_arg_t * pcreate_arg,
                                     cache_entry_t * pentry_dir_prev,
                                     hash_table_t * ht,
                                     cache_inode_client_t * pclient,
                                     fsal_op_context_t * pcontext,
                                     unsigned int create_flag,
                                     cache_inode_status_t * pstatus)
{
  cache_entry_t *pentry = NULL;
  cache_inode_dir_data_t *pdir_data = NULL;
  hash_buffer_t key, value;
  fsal_attrib_list_t fsal_attributes;
  fsal_status_t fsal_status;
  cache_content_status_t cache_content_status;
  int i = 0;
  int rc = 0;
  off_t size_in_cache;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stat */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_NEW_ENTRY] += 1;

  /* Turn the input to a hash key */
  if(cache_inode_fsaldata_2_key(&key, pfsdata, NULL))
    {
      *pstatus = CACHE_INODE_UNAPPROPRIATED_KEY;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY] += 1;
      cache_inode_release_fsaldata_key(&key, pclient);

      return NULL;
    }

  /* Check if the entry doesn't already exists */
  if(HashTable_Get(ht, &key, &value) == HASHTABLE_SUCCESS)
    {
      /* Entry is already in the cache, do not add it */
      pentry = (cache_entry_t *) value.pdata;
      *pstatus = CACHE_INODE_ENTRY_EXISTS;

      LogDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Trying to add an already existing entry. Found entry type: %d State: %d, New type: %d",
                        pentry->internal_md.type, pentry->internal_md.valid_state, type);

      /* stat */
      pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_NEW_ENTRY] += 1;

      return pentry;
    }

  GET_PREALLOC(pentry,
               pclient->pool_entry, pclient->nb_prealloc, cache_entry_t, next_alloc);
  if(pentry == NULL)
    {
      LogDebug(COMPONENT_CACHE_INODE,
                        "Can't allocate a new entry from cache pool");
      *pstatus = CACHE_INODE_MALLOC_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY] += 1;

      return NULL;
    }

  /* if entry is of tyep DIR_CONTINUE or DIR_BEGINNING, it should have a pdir_data */
  if(type == DIR_BEGINNING || type == DIR_CONTINUE)
    {
      GET_PREALLOC(pdir_data,
                   pclient->pool_dir_data,
                   pclient->nb_pre_dir_data, cache_inode_dir_data_t, next_alloc);
      if(pdir_data == NULL)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                            "Can't allocate a new dir_data from cache pool");
          *pstatus = CACHE_INODE_MALLOC_ERROR;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY] += 1;

          return NULL;
        }

      if(type == DIR_BEGINNING)
        pentry->object.dir_begin.pdir_data = pdir_data;
      else
        pentry->object.dir_cont.pdir_data = pdir_data;
    }

  /*  if( type == DIR_BEGINNING || type == DIR_CONTINUE ) */

  if(rw_lock_init(&(pentry->lock)) != 0)
    {
      RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);
      LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_PTHREAD_MUTEX_INIT, errno);
      *pstatus = CACHE_INODE_INIT_ENTRY_FAILED;

      /* stat */
      pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_NEW_ENTRY] += 1;

      return NULL;
    }

  /* Call FSAL to get information about the object if not provided, except for DIR_CONTINUE 
   * that points to their DIR_BEGINNING .
   * If attributes are provided as pfsal_attr parameter, use them. Call FSAL_getattrs otherwise. */
  if(pfsal_attr == NULL)
    {
      /* No attributes are provided, use FSAL_getattrs to query them. */
      if(type != DIR_CONTINUE)
        {
          fsal_attributes.asked_attributes = pclient->attrmask;
          fsal_status = FSAL_getattrs(&pfsdata->handle, pcontext, &fsal_attributes);

          if(FSAL_IS_ERROR(fsal_status))
            {
              /* Put the entry back in its pool */
              LogDebug(COMPONENT_CACHE_INODE,
                                "cache_inode_new_entry: FSAL_getattrs failed");
              RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);
              *pstatus = cache_inode_error_convert(fsal_status);

              if(fsal_status.major == ERR_FSAL_STALE)
                {
                  cache_inode_status_t kill_status;

                  LogEvent(COMPONENT_CACHE_INODE,
                      "cache_inode_new_entry: Stale FSAL File Handle detected for pentry = %p",
                       pentry);

                  if(cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
                     CACHE_INODE_SUCCESS)
                    LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Could not kill entry %p, status = %u",
                         pentry, kill_status);

                }
              /* stat */
              pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY] += 1;

              return NULL;
            }
        }
    }
  else
    {
      /* Use the provided attributes */
      fsal_attributes = *pfsal_attr;
    }

  /* Init the internal metadata */
  pentry->internal_md.type = type;
  pentry->internal_md.valid_state = VALID;
  pentry->internal_md.read_time = 0;
  pentry->internal_md.mod_time = pentry->internal_md.alloc_time = time(NULL);
  pentry->internal_md.refresh_time = pentry->internal_md.alloc_time;

  pentry->gc_lru_entry = NULL;
  pentry->gc_lru = NULL;

  /* No parent for now, it will be added in cache_inode_add_cached_dirent */
  pentry->parent_list = NULL;

  switch (type)
    {
    case REGULAR_FILE:
      LogFullDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Adding a REGULAR_FILE");

      pentry->object.file.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.file.handle;
#ifdef _USE_MFSL_PROXY
      pentry->mobject.plock = &pentry->lock;
#endif
#endif
      pentry->object.file.attributes = fsal_attributes;
      pentry->object.file.pentry_content = NULL;        /* Not yet a File Content entry associated with this entry */
      pentry->object.file.pstate_head = NULL;   /* No associated client yet                                */
      pentry->object.file.pstate_tail = NULL;   /* No associated client yet                                */
      pentry->object.file.open_fd.fileno = 0;
      pentry->object.file.open_fd.last_op = 0;
      pentry->object.file.open_fd.openflags = 0;
      memset(&(pentry->object.file.open_fd.fd), 0, sizeof(fsal_file_t));
      memset(&(pentry->object.file.unstable_data), 0,
             sizeof(cache_inode_unstable_data_t));
#ifdef _USE_PROXY
      pentry->object.file.pname = NULL;
      pentry->object.file.pentry_parent_open = NULL;
#endif

#ifdef _USE_PNFS
      pentry->object.file.pnfs_file.ds_file.allocated = FALSE;
#endif

      break;

    case DIR_BEGINNING:
      LogFullDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Adding a DIR_BEGINNING");

      pentry->object.dir_begin.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.dir_begin.handle;
#endif
      pentry->object.dir_begin.attributes = fsal_attributes;

      pentry->object.dir_begin.has_been_readdir = CACHE_INODE_NO;
      pentry->object.dir_begin.end_of_dir = END_OF_DIR;
      pentry->object.dir_begin.pdir_cont = NULL;
      pentry->object.dir_begin.pdir_last = pentry;
      pentry->object.dir_begin.nbactive = 0;
      pentry->object.dir_begin.nbdircont = 0;
      pentry->object.dir_begin.referral = NULL;

      for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
        {
          pentry->object.dir_begin.pdir_data->dir_entries[i].active = INVALID;
          pentry->object.dir_begin.pdir_data->dir_entries[i].pentry = NULL;
          FSAL_str2name("", 1, &pentry->object.dir_begin.pdir_data->dir_entries[i].name);
        }

      break;

    case DIR_CONTINUE:
      LogFullDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Adding a DIR_CONTINUE");

      pentry->object.dir_cont.end_of_dir = END_OF_DIR;
      pentry->object.dir_cont.pdir_cont = NULL; /* The last entry has no next entry */
      pentry->object.dir_cont.pdir_prev = pentry_dir_prev;

      switch (pentry_dir_prev->internal_md.type)
        {
        case DIR_BEGINNING:
          pentry->object.dir_cont.pdir_begin = pentry_dir_prev;
          pentry->object.dir_cont.dir_cont_pos = 1;     /* The first after the DIR_BEGINNIG */
          break;

        case DIR_CONTINUE:
          pentry->object.dir_cont.pdir_begin =
              pentry_dir_prev->object.dir_cont.pdir_begin;
          pentry->object.dir_cont.dir_cont_pos =
              pentry_dir_prev->object.dir_cont.dir_cont_pos + 1;
          break;

        default:
          *pstatus = CACHE_INODE_NOT_A_DIRECTORY;
          RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY] += 1;

          return NULL;
          break;
        }
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.dir_cont.pdir_begin->mobject.handle;
#endif
      pentry->object.dir_cont.nbactive = 0;
      for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
        {
          pentry->object.dir_cont.pdir_data->dir_entries[i].active = INVALID;
          pentry->object.dir_cont.pdir_data->dir_entries[i].pentry = NULL;
          FSAL_str2name("", 1, &pentry->object.dir_cont.pdir_data->dir_entries[i].name);
        }
      break;

    case SYMBOLIC_LINK:
      LogFullDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Adding a SYMBOLIC_LINK");

      pentry->object.symlink.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.symlink.handle;
#endif
      pentry->object.symlink.attributes = fsal_attributes;
      fsal_status =
          FSAL_pathcpy(&pentry->object.symlink.content, &pcreate_arg->link_content);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          LogDebug(COMPONENT_CACHE_INODE,
                            "cache_inode_new_entry: FSAL_pathcpy failed");
          RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);
        }

      break;

    case SOCKET_FILE:
      LogFullDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Adding a SOCKET_FILE");

      pentry->object.special_obj.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.special_obj.handle;
#endif
      pentry->object.special_obj.attributes = fsal_attributes;
      break;

    case FIFO_FILE:
      LogFullDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Adding a FIFO_FILE");

      pentry->object.special_obj.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.special_obj.handle;
#endif
      pentry->object.special_obj.attributes = fsal_attributes;
      break;

    case BLOCK_FILE:
      LogFullDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Adding a BLOCK_FILE");

      pentry->object.special_obj.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.special_obj.handle;
#endif
      pentry->object.special_obj.attributes = fsal_attributes;
      break;

    case CHARACTER_FILE:
      LogFullDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Adding a CHARACTER_FILE");

      pentry->object.special_obj.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.special_obj.handle;
#endif
      pentry->object.special_obj.attributes = fsal_attributes;
      break;

    default:
      /* Should never happen */
      *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;
      LogMajor(COMPONENT_CACHE_INODE,
                        "/!\\ | cache_inode_new_entry: unknown type provided");
      RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY] += 1;

      return NULL;
    }

  /* Turn the input to a hash key */
  if(cache_inode_fsaldata_2_key(&key, pfsdata, pclient))
    {
      *pstatus = CACHE_INODE_UNAPPROPRIATED_KEY;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY] += 1;
      cache_inode_release_fsaldata_key(&key, pclient);

      return NULL;
    }

  /* Adding the entry in the cache */
  value.pdata = (caddr_t) pentry;
  value.len = sizeof(cache_entry_t);

  if((rc =
      HashTable_Test_And_Set(ht, &key, &value,
                             HASHTABLE_SET_HOW_SET_NO_OVERWRITE)) != HASHTABLE_SUCCESS)
    {
      /* Put the entry back in its pool */
      RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);
      LogEvent(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: entry could not be added to hash, rc=%d",
                        rc);

      *pstatus = CACHE_INODE_HASH_SET_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY] += 1;

      return NULL;
    }

  /* if entry is a REGULAR_FILE and has a related data cache entry from a previous server instance that crashed, recover it */
  /* This is done only when this is not a creation (when creating a new file, it is impossible to have it cached)           */
  if(type == REGULAR_FILE && create_flag == FALSE)
    {
      cache_content_test_cached(pentry,
                                (cache_content_client_t *) pclient->pcontent_client,
                                pcontext, &cache_content_status);

      if(cache_content_status == CACHE_CONTENT_SUCCESS)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                            "cache_inode_new_entry: Entry %p is already datacached, recovering...",
                            pentry);

          /* Adding the cached entry to the data cache */
          if((pentry->object.file.pentry_content = cache_content_new_entry(pentry,
                                                                           NULL,
                                                                           (cache_content_client_t
                                                                            *)
                                                                           pclient->
                                                                           pcontent_client,
                                                                           RECOVER_ENTRY,
                                                                           pcontext,
                                                                           &cache_content_status))
             == NULL)
            {
              LogCrit(COMPONENT_CACHE_INODE,
                           "Error recovering cached data for pentry %p", pentry);
            }
          else
            LogDebug(COMPONENT_CACHE_INODE,
                              "Cached data added successfully for pentry %p", pentry);

          /* Recover the size from the data cache too... */
          if((size_in_cache =
              cache_content_get_cached_size((cache_content_entry_t *) pentry->object.
                                            file.pentry_content)) == -1)
            {
              LogCrit(COMPONENT_CACHE_INODE,
                           "Error when recovering size in cache for pentry %p", pentry);
            }
          else
            pentry->object.file.attributes.filesize = (fsal_size_t) size_in_cache;

        }
    }

  /* Final step */
  P_w(&pentry->lock);
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_GET, pclient);
  V_w(&pentry->lock);

  LogFullDebug(COMPONENT_CACHE_INODE,
                    "cache_inode_new_entry: New entry %p added", pentry);
  *pstatus = CACHE_INODE_SUCCESS;

  /* stat */
  pclient->stat.func_stats.nb_success[CACHE_INODE_NEW_ENTRY] += 1;

  return pentry;
}                               /* cache_inode_new_entry */

/**
 *
 * cache_clean_entry: cleans an entry when it is garbagge collected.
 *
 * Cleans an entry when it is garbagge collected.
 *
 * @param pentry [INOUT] the entry to be cleaned. 
 *
 * @return CACHE_INODE_SUCCESS in all cases. 
 * 
 */
cache_inode_status_t cache_inode_clean_entry(cache_entry_t * pentry)
{
  pentry->internal_md.type = RECYCLED;
  pentry->internal_md.valid_state = INVALID;
  pentry->internal_md.read_time = 0;
  pentry->internal_md.mod_time = 0;
  pentry->internal_md.refresh_time = 0;
  pentry->internal_md.alloc_time = 0;
  return CACHE_INODE_SUCCESS;
}

/**
 *
 * cache_inode_error_convert: converts an FSAL error to the corresponding cache_inode error.
 *
 * Converts an FSAL error to the corresponding cache_inode error.
 *
 * @param fsal_status [IN] fsal error to be converted.
 *
 * @return the result of the conversion.
 * 
 */
cache_inode_status_t cache_inode_error_convert(fsal_status_t fsal_status)
{
  switch (fsal_status.major)
    {
    case ERR_FSAL_NO_ERROR:
      return CACHE_INODE_SUCCESS;
      break;

    case ERR_FSAL_NOENT:
      return CACHE_INODE_NOT_FOUND;
      break;

    case ERR_FSAL_EXIST:
      return CACHE_INODE_ENTRY_EXISTS;
      break;

    case ERR_FSAL_ACCESS:
      return CACHE_INODE_FSAL_EACCESS;
      break;

    case ERR_FSAL_PERM:
      return CACHE_INODE_FSAL_EPERM;
      break;

    case ERR_FSAL_NOSPC:
      return CACHE_INODE_NO_SPACE_LEFT;
      break;

    case ERR_FSAL_NOTEMPTY:
      return CACHE_INODE_DIR_NOT_EMPTY;
      break;

    case ERR_FSAL_ROFS:
      return CACHE_INODE_READ_ONLY_FS;
      break;

    case ERR_FSAL_NOTDIR:
      return CACHE_INODE_NOT_A_DIRECTORY;
      break;

    case ERR_FSAL_IO:
      return CACHE_INODE_IO_ERROR;
      break;

    case ERR_FSAL_STALE:
      return CACHE_INODE_FSAL_ESTALE;
      break;

    case ERR_FSAL_INVAL:
      return CACHE_INODE_INVALID_ARGUMENT;
      break;

    case ERR_FSAL_DQUOT:
      return CACHE_INODE_QUOTA_EXCEEDED;
      break;

    case ERR_FSAL_SEC:
      return CACHE_INODE_FSAL_ERR_SEC;
      break;

    case ERR_FSAL_NOTSUPP:
      return CACHE_INODE_NOT_SUPPORTED;
      break;

    case ERR_FSAL_DELAY:
      return CACHE_INODE_FSAL_DELAY;
      break;

    default:
      /* generic FSAL error */
      LogEvent(COMPONENT_CACHE_INODE,
          "cache_inode_error_convert: default conversion to CACHE_INODE_FSAL_ERROR for error %d,%d",
           fsal_status.major, fsal_status.minor);
      return CACHE_INODE_FSAL_ERROR;
      break;
    }

  /* We should never reach this line, this may produce a warning with certain compiler */
  LogEvent(COMPONENT_CACHE_INODE,
      "cache_inode_error_convert: default conversion to CACHE_INODE_FSAL_ERROR for error %d, this line should never be reached",
       fsal_status.major, __LINE__);
  return CACHE_INODE_FSAL_ERROR;
}                               /* cache_inode_error_convert */

/**
 *
 * cache_inode_valid: validates an entry to update its garbagge status. 
 *
 * Validates an error to update its garbagge status. 
 * Entry is supposed to be locked when this function is called !!
 *
 * @param pentry [INOUT] entry to be validated. 
 * @param op [IN] can be set to CACHE_INODE_OP_GET or CACHE_INODE_OP_SET to show the type of operation done.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 *
 * @return CACHE_INODE_SUCCESS if successful \n
 * @return CACHE_INODE_LRU_ERROR if an errorr occured in LRU management.
 * 
 */
cache_inode_status_t cache_inode_valid(cache_entry_t * pentry,
                                       cache_inode_op_t op,
                                       cache_inode_client_t * pclient)
{
  /* /!\ NOTE THIS CAREFULLY: entry is supposed to be locked when this function is called !! */

  cache_inode_status_t cache_status;
  cache_content_status_t cache_content_status;
  LRU_status_t lru_status;
  LRU_entry_t *plru_entry = NULL;
  cache_content_client_t *pclient_content = NULL;
  cache_content_entry_t *pentry_content = NULL;
#ifndef _NO_BUDDY_SYSTEM
  buddy_stats_t __attribute__ ((__unused__)) bstats;
#endif

  if(pentry == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* for DIR_CONTINUES, process the call on main dir entry */
  if(pentry->internal_md.type == DIR_CONTINUE)
    {
      return cache_inode_valid(pentry->object.dir_cont.pdir_begin, op, pclient);
    }

  /* Invalidate former entry if needed */
  if(pentry->gc_lru != NULL && pentry->gc_lru_entry)
    {
      if(LRU_invalidate(pentry->gc_lru, pentry->gc_lru_entry) != LRU_LIST_SUCCESS)
        {
          RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);
          return CACHE_INODE_LRU_ERROR;
        }
    }

  if((plru_entry = LRU_new_entry(pclient->lru_gc, &lru_status)) == NULL)
    {
      RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);
      return CACHE_INODE_LRU_ERROR;
    }
  plru_entry->buffdata.pdata = (caddr_t) pentry;
  plru_entry->buffdata.len = sizeof(cache_entry_t);

  /* Setting the anchors */
  pentry->gc_lru = pclient->lru_gc;
  pentry->gc_lru_entry = plru_entry;

  /* Update internal md */
  pentry->internal_md.valid_state = VALID;

  if(op == CACHE_INODE_OP_GET)
    pentry->internal_md.read_time = time(NULL);

  if(op == CACHE_INODE_OP_SET)
    {
      pentry->internal_md.mod_time = time(NULL);
      pentry->internal_md.refresh_time = pentry->internal_md.mod_time;
    }

  /* Add a call to the GC counter */
  pclient->call_since_last_gc += 1;

  /* If open/close fd cache is used for FSAL, manage it here */
    LogFullDebug(COMPONENT_CACHE_INODE, "--------> use_cache=%u fileno=%d last_op=%u time(NULL)=%u delta=%d retention=%u\n",
       pclient->use_cache, pentry->object.file.open_fd.fileno,
       pentry->object.file.open_fd.last_op, time(NULL),
       time(NULL) - pentry->object.file.open_fd.last_op, pclient->retention);

  if(pentry->internal_md.type == REGULAR_FILE)
    {
      if(pclient->use_cache == 1)
        {
          if(pentry->object.file.open_fd.fileno != 0)
            {
              if(time(NULL) - pentry->object.file.open_fd.last_op > pclient->retention)
                {
                  if(cache_inode_close(pentry, pclient, &cache_status) !=
                     CACHE_INODE_SUCCESS)
                    {
                      /* Bad close */
                      return cache_status;
                    }
                }
            }
        }

      /* Same of local fd cache */
      pclient_content = (cache_content_client_t *) pclient->pcontent_client;
      pentry_content = (cache_content_entry_t *) pentry->object.file.pentry_content;

      if(pentry_content != NULL)
        if(pclient_content->use_cache == 1)
          if(pentry_content->local_fs_entry.opened_file.local_fd > 0)
            if(time(NULL) - pentry_content->local_fs_entry.opened_file.last_op >
               pclient_content->retention)
              if(cache_content_close
                 (pentry_content, pclient_content,
                  &cache_content_status) != CACHE_CONTENT_SUCCESS)
                return CACHE_INODE_CACHE_CONTENT_ERROR;
    }


#ifndef _NO_BUDDY_SYSTEM

#if 0
  BuddyGetStats(&bstats);
  LogFullDebug(COMPONENT_CACHE_INODE,
      "(pthread_self=%u) NbStandard=%lu  NbStandardUsed=%lu  InsideStandard(nb=%lu, size=%lu)\n",
       (unsigned int)pthread_self(), (long unsigned int)bstats.NbStdPages, (long unsigned int)bstats.NbStdUsed, (long unsigned int)bstats.StdUsedSpace,
       (long unsigned int)bstats.NbStdUsed);
#endif

#endif
  LogFullDebug(COMPONENT_CACHE_INODE,
      "(pthread_self=%u) LRU GC state: nb_entries=%d nb_invalid=%d nb_call_gc=%d param.nb_call_gc_invalid=%d\n",
       pthread_self(), pclient->lru_gc->nb_entry, pclient->lru_gc->nb_invalid,
       pclient->lru_gc->nb_call_gc, pclient->lru_gc->parameter.nb_call_gc_invalid);

  LogFullDebug(COMPONENT_CACHE_INODE,
      "LRU GC state: nb_entries=%d nb_invalid=%d nb_call_gc=%d param.nb_call_gc_invalid=%d\n",
       pclient->lru_gc->nb_entry, pclient->lru_gc->nb_invalid,
       pclient->lru_gc->nb_call_gc, pclient->lru_gc->parameter.nb_call_gc_invalid);

  LogFullDebug(COMPONENT_CACHE_INODE,
                    "LRU GC state: nb_entries=%d nb_invalid=%d nb_call_gc=%d param.nb_call_gc_invalid=%d",
                    pclient->lru_gc->nb_entry, pclient->lru_gc->nb_invalid,
                    pclient->lru_gc->nb_call_gc,
                    pclient->lru_gc->parameter.nb_call_gc_invalid);


  /* Call LRU_gc_invalid to get ride of the unused invalid lru entries */
  if(LRU_gc_invalid(pclient->lru_gc, NULL) != LRU_LIST_SUCCESS)
    return CACHE_INODE_LRU_ERROR;

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_valid */

/**
 *
 * cache_inode_get_attributes: gets the attributes cached in the entry.
 *
 * Gets the attributes cached in the entry.
 *
 * @param pentry [IN] the entry to deal with.
 * @param pattr [OUT] the attributes for this entry.
 *
 * @return nothing (void function).
 * 
 */
void cache_inode_get_attributes(cache_entry_t * pentry, fsal_attrib_list_t * pattr)
{
  /* The pentry is supposed to be locked */
  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      *pattr = pentry->object.file.attributes;
      break;

    case SYMBOLIC_LINK:
      *pattr = pentry->object.symlink.attributes;
      break;

    case DIR_BEGINNING:
      *pattr = pentry->object.dir_begin.attributes;
      break;

    case DIR_CONTINUE:
      /* lock the related dir_begin (dir begin are garbagge collected AFTER their related dir_cont)
       * this means that if a DIR_CONTINUE exists, its pdir pointer is not endless */
      P_r(&pentry->object.dir_cont.pdir_begin->lock);
      *pattr = pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes;
      V_r(&pentry->object.dir_cont.pdir_begin->lock);
      break;

    case SOCKET_FILE:
    case FIFO_FILE:
    case BLOCK_FILE:
    case CHARACTER_FILE:
      *pattr = pentry->object.special_obj.attributes;
      break;

    }
}                               /* cache_inode_get_attributes */

/**
 *
 * cache_inode_set_attributes: sets the attributes cached in the entry.
 *
 * Sets the attributes cached in the entry.
 *
 * @param pentry [OUT] the entry to deal with.
 * @param pattr [IN] the attributes to be set for this entry.
 *
 * @return nothing (void function).
 * 
 */
void cache_inode_set_attributes(cache_entry_t * pentry, fsal_attrib_list_t * pattr)
{
  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      pentry->object.file.attributes = *pattr;
      break;

    case SYMBOLIC_LINK:
      pentry->object.symlink.attributes = *pattr;
      break;

    case DIR_BEGINNING:
      pentry->object.dir_begin.attributes = *pattr;
      break;

    case DIR_CONTINUE:
      /* lock the related dir_begin (dir begin are garbagge collected AFTER their related dir_cont)
       * this means that if a DIR_CONTINUE exists, its pdir pointer is not endless */
      P_r(&pentry->object.dir_cont.pdir_begin->lock);
      pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes = *pattr;
      V_r(&pentry->object.dir_cont.pdir_begin->lock);
      break;

    case SOCKET_FILE:
    case FIFO_FILE:
    case BLOCK_FILE:
    case CHARACTER_FILE:
      pentry->object.special_obj.attributes = *pattr;
      break;
    }
}                               /* cache_inode_set_attributes */

/**
 *
 * cache_inode_fsal_type_convert: converts an FSAL type to the cache_inode type to be used.
 *
 * Converts an FSAL type to the cache_inode type to be used.
 *
 * @param type [IN] the input FSAL type. 
 *
 * @return the result of the conversion.
 * 
 */
cache_inode_file_type_t cache_inode_fsal_type_convert(fsal_nodetype_t type)
{
  cache_inode_file_type_t rctype;

  switch (type)
    {
    case FSAL_TYPE_DIR:
      rctype = DIR_BEGINNING;
      break;

    case FSAL_TYPE_FILE:
      rctype = REGULAR_FILE;
      break;

    case FSAL_TYPE_LNK:
      rctype = SYMBOLIC_LINK;
      break;

    case FSAL_TYPE_BLK:
      rctype = BLOCK_FILE;
      break;

    case FSAL_TYPE_FIFO:
      rctype = FIFO_FILE;
      break;

    case FSAL_TYPE_CHR:
      rctype = CHARACTER_FILE;
      break;

    case FSAL_TYPE_SOCK:
      rctype = SOCKET_FILE;
      break;

    default:
      rctype = UNASSIGNED;
      break;
    }

  return rctype;
}                               /* cache_inode_fsal_type_convert */

/**
 *
 * cache_inode_get_fsal_handle: gets the FSAL handle from a pentry.
 *
 * Gets the FSAL handle from a pentry. The entry should be lock BEFORE this call is done (no lock is managed
 * in this function). All DIR_BEGINNING and DIR_CONTINUE involved in the same dir_chain will return the same handle.
 *
 * @param pentry [IN] the input pentry.
 * @param pstatus [OUT] the status for the extraction (If not CACHE_INODE_SUCCESS, there is an error).
 *
 * @return the result of the conversion. NULL shows an error.
 * 
 */
fsal_handle_t *cache_inode_get_fsal_handle(cache_entry_t * pentry,
                                           cache_inode_status_t * pstatus)
{
  fsal_handle_t *preturned_handle = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  if(pentry == NULL)
    {
      preturned_handle = NULL;
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
    }
  else
    {
      switch (pentry->internal_md.type)
        {
        case REGULAR_FILE:
          preturned_handle = &pentry->object.file.handle;
          *pstatus = CACHE_INODE_SUCCESS;
          break;

        case SYMBOLIC_LINK:
          preturned_handle = &pentry->object.symlink.handle;
          *pstatus = CACHE_INODE_SUCCESS;
          break;

        case DIR_BEGINNING:
          preturned_handle = &pentry->object.dir_begin.handle;
          *pstatus = CACHE_INODE_SUCCESS;
          break;

        case DIR_CONTINUE:
          preturned_handle = &pentry->object.dir_cont.pdir_begin->object.dir_begin.handle;
          *pstatus = CACHE_INODE_SUCCESS;
          break;

        case SOCKET_FILE:
        case FIFO_FILE:
        case BLOCK_FILE:
        case CHARACTER_FILE:
          preturned_handle = &pentry->object.special_obj.handle;
          break;

        default:
          preturned_handle = NULL;
          *pstatus = CACHE_INODE_BAD_TYPE;
          break;
        }                       /* switch( pentry->internal_md.type ) */
    }

  return preturned_handle;
}                               /* cache_inode_get_fsal_handle */

/**
 *
 * cache_inode_type_are_rename_compatible: test if an existing entry could be scrtached during a rename.
 *
 * test if an existing entry could be scrtached during a rename. No mutext management.
 *
 * @param pentry_src  [IN] the source pentry (the one to be renamed)
 * @param pentry_dest [IN] the dest pentry (the one to be scratched during the rename)
 *
 * @return TRUE if rename if allowed (types are compatible), FALSE if not.
 * 
 */
int cache_inode_type_are_rename_compatible(cache_entry_t * pentry_src,
                                           cache_entry_t * pentry_dest)
{
  /* Manage the case where pentry is DIR_CONTINUE */
  if(pentry_src->internal_md.type == DIR_CONTINUE)
    return cache_inode_type_are_rename_compatible(pentry_src->object.dir_cont.pdir_begin,
                                                  pentry_dest);

  if(pentry_dest->internal_md.type == DIR_CONTINUE)
    return cache_inode_type_are_rename_compatible(pentry_src,
                                                  pentry_dest->object.dir_cont.
                                                  pdir_begin);

  /* TRUE is both entries are non directories or to directories and the second is empty */
  if(pentry_src->internal_md.type == DIR_BEGINNING)
    {
      if(pentry_dest->internal_md.type == DIR_BEGINNING)
        {
          if(cache_inode_is_dir_empty(pentry_dest) == CACHE_INODE_SUCCESS)
            return TRUE;
          else
            return FALSE;
        }
      else
        return FALSE;
    }
  else
    {
      /* pentry_src is not a directory */
      if(pentry_dest->internal_md.type == DIR_BEGINNING)
        return FALSE;
      else
        return TRUE;
    }
}                               /* cache_inode_type_are_rename_compatible */

/**
 *
 * cache_inode_mutex_destroy: destroys the pthread_mutex associated with a pentry when it is put back to the spool.
 *
 * Destroys the pthread_mutex associated with a pentry when it is put back to the spool
 *
 * @param pentry [INOUT] the input pentry.
 *
 * @return nothing (void function)
 * 
 */
void cache_inode_mutex_destroy(cache_entry_t * pentry)
{
  rw_lock_destroy(&pentry->lock);
}                               /* cache_inode_mutex_destroy */

/**
 *
 * cache_inode_print_dir: prints the content of a pentry that is a directory segment. 
 *
 * Prints the content of a pentry that is a DIR_BEGINNING or a DIR_CONTINUE. 
 * /!\ This function is provided for debugging purpose only, it makes no sanity check on the arguments. 
 *
 * @param pentry [IN] the input pentry.
 *
 * @return nothing (void function)
 * 
 */
void cache_inode_print_dir(cache_entry_t * cache_entry_root)
{
  cache_entry_t *cache_entry_iter = NULL;
  int i = 0;

  if(cache_entry_root->internal_md.type != DIR_BEGINNING &&
     cache_entry_root->internal_md.type != DIR_CONTINUE)
    {
      LogFullDebug(COMPONENT_CACHE_INODE, "This entry is not a directory segment\n");
      return;
    }

  cache_entry_iter = cache_entry_root;

  while(cache_entry_iter != NULL)
    {
      if(cache_entry_iter->internal_md.type == DIR_BEGINNING)
        {
          for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
            LogFullDebug(COMPONENT_CACHE_INODE, "Name = %s, DIR_BEGINNING entry = %p, active=%d, i=%d\n",
                   cache_entry_iter->object.dir_begin.pdir_data->dir_entries[i].name.name,
                   cache_entry_iter->object.dir_begin.pdir_data->dir_entries[i].pentry,
                   cache_entry_iter->object.dir_begin.pdir_data->dir_entries[i].active,
                   i);

          cache_entry_iter = cache_entry_iter->object.dir_begin.pdir_cont;
        }
      else
        {
          for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
            LogFullDebug(COMPONENT_CACHE_INODE, "Name = %s, DIR_CONTINUE entry = %p, active=%d, i=%d\n",
                   cache_entry_iter->object.dir_cont.pdir_data->dir_entries[i].name.name,
                   cache_entry_iter->object.dir_cont.pdir_data->dir_entries[i].pentry,
                   cache_entry_iter->object.dir_cont.pdir_data->dir_entries[i].active, i);

          cache_entry_iter = cache_entry_iter->object.dir_cont.pdir_cont;
        }
    }
  LogFullDebug(COMPONENT_CACHE_INODE,"------------------\n");
}                               /* cache_inode_print_dir */

/**
 *
 * cache_inode_dump_content: dumps the content of a pentry to a local file (used for File Content index files).
 *
 * Dumps the content of a pentry to a local file (used for File Content index files).
 *
 * @param path [IN] the full path to the file that will contain the data.
 * @param pentry [IN] the input pentry.
 *
 * @return CACHE_INODE_BAD_TYPE if pentry is not related to a REGULAR_FILE \n
 * @return CACHE_INODE_INVALID_ARGUMENT if path is inconsistent \n
 * @return CACHE_INODE_SUCCESS if operation succeded. 
 * 
 */
cache_inode_status_t cache_inode_dump_content(char *path, cache_entry_t * pentry)
{
  FILE *stream = NULL;

  char buff[CACHE_INODE_DUMP_LEN];

  if(pentry->internal_md.type != REGULAR_FILE)
    return CACHE_INODE_BAD_TYPE;

  /* Open the index file */
  if((stream = fopen(path, "w")) == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Dump the information */
  fprintf(stream, "internal:read_time=%d\n", (int)pentry->internal_md.read_time);
  fprintf(stream, "internal:mod_time=%d\n", (int)pentry->internal_md.mod_time);
  fprintf(stream, "internal:export_id=%d\n", 0);

  snprintHandle(buff, CACHE_INODE_DUMP_LEN, &(pentry->object.file.handle));
  fprintf(stream, "file: FSAL handle=%s", buff);

  /* Close the handle */
  fclose(stream);

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_dump_content */

/**
 *
 * cache_inode_reload_content: reloads the content of a pentry from a local file (used File Content crash recovery).
 *
 * Reloeads the content of a pentry from a local file (used File Content crash recovery).
 *
 * @param path [IN] the full path to the file that will contain the metadata.
 * @param pentry [IN] the input pentry.
 *
 * @return CACHE_INODE_BAD_TYPE if pentry is not related to a REGULAR_FILE \n
 * @return CACHE_INODE_SUCCESS if operation succeded. 
 * 
 */
cache_inode_status_t cache_inode_reload_content(char *path, cache_entry_t * pentry)
{
  FILE *stream = NULL;

  char buff[CACHE_INODE_DUMP_LEN];

  /* Open the index file */
  if((stream = fopen(path, "r")) == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* The entry is a file (only file inode are dumped), in state VALID for the gc (not garbageable) */
  pentry->internal_md.type = REGULAR_FILE;
  pentry->internal_md.valid_state = VALID;

  /* Read the information */
  fscanf(stream, "internal:read_time=%s\n", buff);
  pentry->internal_md.read_time = atoi(buff);

  fscanf(stream, "internal:mod_time=%s\n", buff);
  pentry->internal_md.mod_time = atoi(buff);

  fscanf(stream, "internal:export_id=%s\n", buff);

  fscanf(stream, "file: FSAL handle=%s", buff);
  if(sscanHandle(&(pentry->object.file.handle), buff) < 0)
    {
      /* expected = 2*sizeof(fsal_handle_t) in hexa representation */
      LogCrit(COMPONENT_CACHE_INODE,
          "Error recovering cache content index %s: Invalid handle length. Expected length=%u, Found=%u",
           path, (unsigned int)(2 * sizeof(fsal_handle_t)), (unsigned int)strlen(buff));

      return CACHE_INODE_INCONSISTENT_ENTRY;
    }

  /* Close the handle */
  fclose(stream);

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_reload_content */

/**
 *
 * cache_inode_invalidate_related_dirent: sets the related directory entries as invalid.
 *
 * sets the related directory entries as invalid. /!\ the parent entry is supposed to be locked.
 *
 * @param pentry [INOUT] entry to be managed
 * @param pclient [IN] related pclient
 *
 * @return  LRU_LIST_SET_INVALID if ok,  LRU_LIST_DO_NOT_SET_INVALID otherwise
 *
 */
static void cache_inode_invalidate_related_dirent(cache_entry_t * pentry,
                                                  cache_inode_client_t * pclient)
{
  cache_inode_parent_entry_t *parent_iter = NULL;

  /* Set the cache status as INVALID in the directory entries */
  for(parent_iter = pentry->parent_list; parent_iter != NULL;
      parent_iter = parent_iter->next_parent)
    {
      if(parent_iter->parent == NULL)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                            "cache_inode_gc_invalidate_related_dirent: pentry %p has no parent, no dirent to be removed...",
                            pentry);
          continue;
        }

      /* If I reached this point, then parent_iter->parent is not null and is a valid cache_inode pentry */
      P_w(&parent_iter->parent->lock);

      /* Check for type of the parent */
      if(parent_iter->parent->internal_md.type != DIR_BEGINNING &&
         parent_iter->parent->internal_md.type != DIR_CONTINUE)
        {
          V_w(&parent_iter->parent->lock);
          /* Major parent incoherency: parent is no directory */
          LogDebug(COMPONENT_CACHE_INODE,
                            "cache_inode_gc_invalidate_related_dirent: major incoherency. Found an entry whose parent is no directory");
          return;
        }

      /* Set the entry as invalid in the dirent array */
      if(parent_iter->parent->internal_md.type == DIR_BEGINNING)
        {
          if(parent_iter->subdirpos > CHILDREN_ARRAY_SIZE)
            {
              V_w(&parent_iter->parent->lock);
              LogFullDebug(COMPONENT_CACHE_INODE,
                  "A known bug occured line %d file %s: pentry=%p type=%u parent_iter->subdirpos=%d, should never exceed, entry not removed %d",
                   __LINE__, __FILE__, pentry, pentry->internal_md.type,
                   parent_iter->subdirpos, CHILDREN_ARRAY_SIZE);
              return;
            }
          else
            {
              parent_iter->parent->object.dir_begin.pdir_data->dir_entries[parent_iter->
                                                                           subdirpos].
                  active = INVALID;
              /* Garbagge invalidates the effet of the readdir previously made */
              parent_iter->parent->object.dir_begin.has_been_readdir = CACHE_INODE_NO;
              parent_iter->parent->object.dir_begin.nbactive -= 1;
            }
        }
      else
        {
          if(parent_iter->subdirpos > CHILDREN_ARRAY_SIZE)
            {
              V_w(&parent_iter->parent->lock);
              LogFullDebug(COMPONENT_CACHE_INODE,
                  "A known bug occured line %d file %s: pentry=%p type=%u parent_iter->subdirpos=%d, should never exceed %d, entry not removed",
                   __LINE__, __FILE__, pentry, pentry->internal_md.type,
                   parent_iter->subdirpos, CHILDREN_ARRAY_SIZE);
              return;
            }
          else
            {
              parent_iter->parent->object.dir_cont.pdir_data->dir_entries[parent_iter->
                                                                          subdirpos].
                  active = INVALID;
              parent_iter->parent->object.dir_cont.nbactive -= 1;
            }
        }

      V_w(&parent_iter->parent->lock);
    }
}                               /* cache_inode_invalidate_related_dirent */

/**
 *
 * cache_inode_kill_entry: force removing an entry from the cache_inode. This is used in case of a 'stale' entry.
 *
 * Force removing an entry from the cache_inode. This is used in case of a 'stale' entry.
 *
 * @param pentry  [IN] the input pentry (supposed to be staled).
 * @param ht      [INOUT] the related hash table for the cache_inode cache.
 * @param pclient [INOUT] related cache_inode client.
 * @param pstatus [OUT] status for the operation.
 *
 * @return CACHE_INODE_BAD_TYPE if pentry is not related a REGULAR_FILE or DIR_BEGINNING \n
 * @return CACHE_INODE_SUCCESS if operation succeded. 
 * 
 */
cache_inode_status_t cache_inode_kill_entry(cache_entry_t * pentry,
                                            hash_table_t * ht,
                                            cache_inode_client_t * pclient,
                                            cache_inode_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  cache_inode_fsal_data_t fsaldata;
  cache_inode_parent_entry_t *parent_iter = NULL;
  cache_inode_parent_entry_t *parent_iter_next = NULL;
  hash_buffer_t key, old_key;
  hash_buffer_t old_value;
  int rc;
  int i;
  fsal_status_t fsal_status;
  cache_entry_t *pentry_iter = NULL;
  cache_entry_t *pentry_iter_save = NULL;
  cache_inode_status_t kill_status;

  LogEvent(COMPONENT_CACHE_INODE, "Using cache_inode_kill_entry for entry %p", pentry);

  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pentry == NULL || pclient == NULL || ht == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Get the FSAL handle */
  if((pfsal_handle = cache_inode_get_fsal_handle(pentry, pstatus)) == NULL)
    {
      LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_kill_entry: unable to retrieve pentry's specific filesystem info");
      return *pstatus;
    }

  /* Invalidate the related LRU gc entry (no more required) */
  if(pentry->gc_lru_entry != NULL)
    {
      if(LRU_invalidate(pentry->gc_lru, pentry->gc_lru_entry) != LRU_LIST_SUCCESS)
        {
          *pstatus = CACHE_INODE_LRU_ERROR;
          return *pstatus;
        }
    }

  fsaldata.handle = *pfsal_handle;

  if(pentry->internal_md.type == DIR_CONTINUE)
    fsaldata.cookie = pentry->object.dir_cont.dir_cont_pos;
  else
    fsaldata.cookie = DIR_START;

  /* Use the handle to build the key */
  if(cache_inode_fsaldata_2_key(&key, &fsaldata, pclient))
    {
      LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_kill_entry: could not build hashtable key");

      cache_inode_release_fsaldata_key(&key, pclient);
      *pstatus = CACHE_INODE_NOT_FOUND;
      return *pstatus;
    }

  /* Remove the whole dir_chain from the cache */
  if(pentry->internal_md.type == DIR_BEGINNING)
    {
      pentry_iter = pentry->object.dir_begin.pdir_cont;
      while(pentry_iter != NULL)
        {
          pentry_iter_save = pentry_iter->object.dir_cont.pdir_cont;

          if(cache_inode_kill_entry(pentry_iter, ht, pclient, &kill_status) !=
             CACHE_INODE_SUCCESS)
            LogCrit(COMPONENT_CACHE_INODE, "cache_inode_kill_entry: could not kill pentry %p of type %u",
                       pentry_iter, pentry_iter->internal_md.type);

          pentry_iter = pentry_iter_save;
        }
    }

  /* Clean parent entries */
  cache_inode_invalidate_related_dirent(pentry, pclient);

  /* use the key to delete the entry */
  if((rc = HashTable_Del(ht, &key, &old_key, &old_value)) != HASHTABLE_SUCCESS)
    {
      LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_kill_entry: entry could not be deleted, status = %d",
                        rc);

      cache_inode_release_fsaldata_key(&key, pclient);

      *pstatus = CACHE_INODE_NOT_FOUND;
      return *pstatus;
    }

  /* Clean up the associated ressources in the FSAL */
  if(FSAL_IS_ERROR(fsal_status = FSAL_CleanObjectResources(pfsal_handle)))
    {
      LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_kill_entry: Could'nt free FSAL ressources fsal_status.major=%u",
                        fsal_status.major);
    }

  /* Release the hash key data */
  cache_inode_release_fsaldata_key(&old_key, pclient);

  /* Sanity check: old_value.pdata is expected to be equal to pentry,
   * and is released later in this function */
  if((cache_entry_t *) old_value.pdata != pentry)
    {
      LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_kill_entry: unexpected pdata %p from hash table (pentry=%p)",
                        old_value.pdata, pentry);
    }

  /* Release the current key */
  cache_inode_release_fsaldata_key(&key, pclient);

  /* Recover the parent list entries */
  parent_iter = pentry->parent_list;
  while(parent_iter != NULL)
    {
      parent_iter_next = parent_iter->next_parent;

      RELEASE_PREALLOC(parent_iter, pclient->pool_parent, next_alloc);

      parent_iter = parent_iter_next;
    }

  /* If entry is datacached, remove it from the cache */
  if(pentry->internal_md.type == REGULAR_FILE)
    {
      cache_content_status_t cache_content_status;

      if(pentry->object.file.pentry_content != NULL)
        if(cache_content_release_entry
           ((cache_content_entry_t *) pentry->object.file.pentry_content,
            (cache_content_client_t *) pclient->pcontent_client,
            &cache_content_status) != CACHE_CONTENT_SUCCESS)
          LogCrit(COMPONENT_CACHE_INODE,
                            "Could not removed datacached entry for pentry %p", pentry);
    }

  /* If entry is a DIR_CONTINUE or a DIR_BEGINNING, release pdir_data */
  if(pentry->internal_md.type == DIR_BEGINNING)
    {
      for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
        {
          pentry->object.dir_begin.pdir_data->dir_entries[i].active = INVALID;
          pentry->object.dir_begin.pdir_data->dir_entries[i].pentry = NULL;
        }
      /* Put the pentry back to the pool */
      RELEASE_PREALLOC(pentry->object.dir_begin.pdir_data, pclient->pool_dir_data,
                       next_alloc);
    }

  if(pentry->internal_md.type == DIR_CONTINUE)
    {
      for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
        {
          pentry->object.dir_cont.pdir_data->dir_entries[i].active = INVALID;
          pentry->object.dir_cont.pdir_data->dir_entries[i].pentry = NULL;
        }
      /* Put the pentry back to the pool */
      RELEASE_PREALLOC(pentry->object.dir_cont.pdir_data, pclient->pool_dir_data,
                       next_alloc);
    }

  /* Destroy the mutex associated with the pentry */
  cache_inode_mutex_destroy(pentry);

  /* Put the pentry back to the pool */
  RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}                               /* cache_inode_kill_entry */

#ifdef _USE_PROXY
void nfs4_sprint_fhandle(nfs_fh4 * fh4p, char *outstr);

void cache_inode_print_srvhandle(char *comment, cache_entry_t * pentry)
{
  fsal_handle_t *pfsal_handle;
  nfs_fh4 nfsfh;
  char tag[30];
  char outstr[1024];

  if(pentry == NULL)
    return;

  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      strcpy(tag, "file");
      pfsal_handle = &(pentry->object.file.handle);
      break;

    case SYMBOLIC_LINK:
      strcpy(tag, "link");
      pfsal_handle = &(pentry->object.symlink.handle);
      break;

    case DIR_BEGINNING:
      strcpy(tag, "dir ");
      pfsal_handle = &(pentry->object.dir_begin.handle);
      break;

    default:
      return;
      break;
    }

  nfsfh.nfs_fh4_len = pfsal_handle->data.srv_handle_len;
  nfsfh.nfs_fh4_val = pfsal_handle->data.srv_handle_val;

  nfs4_sprint_fhandle(&nfsfh, outstr);

  LogFullDebug(COMPONENT_CACHE_INODE, "-->-->-->-->--> External FH (%s) comment=%s = %s", tag, comment, outstr);
}                               /* cache_inode_print_srvhandle */
#endif
