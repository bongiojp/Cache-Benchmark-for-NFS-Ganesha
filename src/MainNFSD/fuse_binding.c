/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est r�gi par la licence CeCILL soumise au droit fran�ais et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffus�e par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilit� au code source et des droits de copie,
 * de modification et de redistribution accord�s par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limit�e.  Pour les m�mes raisons,
 * seule une responsabilit� restreinte p�se sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les conc�dants successifs.
 *
 * A cet �gard  l'attention de l'utilisateur est attir�e sur les risques
 * associ�s au chargement,  � l'utilisation,  � la modification et/ou au
 * d�veloppement et � la reproduction du logiciel par l'utilisateur �tant
 * donn� sa sp�cificit� de logiciel libre, qui peut le rendre complexe �
 * manipuler et qui le r�serve donc � des d�veloppeurs et des professionnels
 * avertis poss�dant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invit�s � charger  et  tester  l'ad�quation  du
 * logiciel � leurs besoins dans des conditions permettant d'assurer la
 * s�curit� de leurs syst�mes et ou de leurs donn�es et, plus g�n�ralement,
 * � l'utiliser et l'exploiter dans les m�mes conditions de s�curit�.
 *
 * Le fait que vous puissiez acc�der � cet en-t�te signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accept� les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2008)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 * ---------------------------------------
 */

/**
 * \file    nfs_main.c
 * \author  $Author: deniel $
 * \brief   The file that contain the 'main' routine for the nfsd.
 *
 * nfs_main.c : The file that contain the 'main' routine for the nfsd.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "ganesha_fuse_wrap.h"
#include "nfs_init.h"
#include "fsal.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h> /* for sigaction */
#include <errno.h>


/* parameters for NFSd startup and default values */

static nfs_start_info_t    nfs_start_info =
{
   .flush_datacache_mode = FALSE,
   .nb_flush_threads     = 1,
   .flush_behaviour      = CACHE_CONTENT_FLUSH_AND_DELETE,
};

static char config_path[MAXPATHLEN] = ""; /* None by default */
char log_path[MAXPATHLEN]    = "/tmp/ganesha_nfsd.log";
char exec_name[MAXPATHLEN]   = "ganesha-nfsd";
char host_name[MAXHOSTNAMELEN] = "localhost";
int          debug_level       = NIV_EVENT;
int          detach_flag       = FALSE ;
int          single_threaded   = FALSE ;
struct ganefuse_operations ops;

char ganesha_exec_path[MAXPATHLEN] ;

/* command line syntax */

char  options[] = "h@Rds:F:S:f:L:N:" ;
char  usage[] = 
    "Usage: %s [-hds][-L <logfile>][-N <dbg_lvl>][-f <config_file>]\n"        
    "\t[-h]                display this help\n"
    "\t[-s]                single-threaded (for MT-unsafe filesystems)\n"
    "\t[-L <logfile>]      set the default logfile for the daemon\n"
    "\t[-N <dbg_lvl>]      set the verbosity level\n"
    "\t[-f <config_file>]  set the config file to be used\n"
    "\t[-d]                the daemon starts in background, in a new process group\n"
    "\t[-R]                daemon will manage RPCSEC_GSS (default is no RPCSEC_GSS)\n"
    "\t[-F] <nb_flushers>  flushes the data cache with purge, but do not answer to requests\n"
    "\t[-S] <nb_flushers>  flushes the data cache without purge, but do not answer to requests\n"
    "----------------- Signals ----------------\n"
    "SIGUSR1    : Enable/Disable File Content Cache forced flush\n" 
    "------------- Default Values -------------\n"
    "LogFile    : /tmp/ganesha_nfsd.log\n"
    "DebugLevel : NIV_EVENT\n"
    "ConfigFile : None\n" ;

/**
 *
 * SIGCHLD signal management routine, for detecting the end of a child process
 *
 */

static void action_sigusr1( int sig )
{
 DisplayLog( "NFS_MAIN_SIGUSR1_HANDLER: Receveid SIGUSR1.... signal will be managed" ) ;
 
  /* Set variable force_flush_by_signal that is used in file content cache gc thread */
  if( force_flush_by_signal )
    {
        DisplayLog( "NFS_MAIN_SIGUSR1_HANDLER: force_flush_by_signal is set to FALSE" ) ;
        force_flush_by_signal = FALSE ;
    }
  else
    {
        DisplayLog( "NFS_MAIN_SIGUSR1_HANDLER: force_flush_by_signal is set to TRUE" ) ;
        force_flush_by_signal = TRUE ;
    }
} /* action_sigusr1 */


static void init_log( log_t * p_log_outputs, char * alt_file )
{
    desc_log_stream_t  log_stream;

    if ( (p_log_outputs->nb_voies == 0) && (strlen(alt_file) > 0) )
    {
        strcpy( log_stream.path , alt_file );
        
        AddLogStreamJd( p_log_outputs, V_FILE, log_stream, debug_level, SUP);        
    }
}

   
/**
 * main: simply the main function.
 *
 * The 'main' function as in every C program. 
 * 
 * @param argc number of arguments
 * @param argv array of arguments
 * 
 * @return status to calling program by calling the exit(3C) function.
 *
 */

int ganefuse_main( int argc, char * argv[],
                   const struct ganefuse_operations *op,
                   void *user_data)
{
    char           * tempo_exec_name          = NULL ;
    char             localmachine[MAXHOSTNAMELEN];
    int              c ;
    nfs_parameter_t  nfs_param;
    pid_t            son_pid ;
    struct sigaction act_sigusr1 ;
    
    int     argc_local = argc;
    char ** argv_local = argv;
    
    /* local copy for keeping it read only */
    ops = *op;


    /* retrieve executable file's name */
    strncpy( ganesha_exec_path, argv[0], MAXPATHLEN ) ;

    if( ( tempo_exec_name = strrchr( argv_local[0], '/' ) ) != NULL )
        strcpy( (char *)exec_name, tempo_exec_name + 1 ) ;

    if( *exec_name == '\0' )
        strcpy( (char *)exec_name, argv_local[0] ) ;
   
    
    /* get host name */
    
    if( gethostname( localmachine, sizeof( localmachine ) ) != 0 )
    {
        fprintf( stderr, "Could not get local host name, exiting..." ) ;
        exit( 1 ) ;
    }
    else
        strncpy( host_name, localmachine, MAXHOSTNAMELEN);
    

    /* now parsing options with getopt */
    while( ( c = getopt( argc_local, argv_local, options ) ) != EOF )
    {
        
      switch( c ) 
        {
        case '@':
          /* A litlle backdoor to keep track of binary versions */
          printf( "%s compiled on %s at %s\n", exec_name, __DATE__, __TIME__ ) ;
          printf( "Release = %s\n", VERSION ) ;
          printf( "Release comment = %s\n", VERSION_COMMENT ) ;
          exit( 0 ) ;
          break ;

        case 'L':
          /* Default Log */
          strncpy( log_path, optarg, MAXPATHLEN ) ;
          break ;

        case 'N':
          /* debug level */
          debug_level = ReturnLevelAscii( optarg );
          if ( debug_level == -1 )
          {
            fprintf( stderr, "Invalid value for option 'N': NIV_NULL, NIV_MAJ, NIV_CRIT, NIV_EVENT, NIV_DEBUG or NIV_FULL_DEBUG expected.\n" );
            exit(1);
          }
          break ;

        case 'f':
          /* config file */
          strncpy( config_path, optarg, MAXPATHLEN ) ;
          break ;

        case 's':
          /* single threaded */
          single_threaded = TRUE;
          break ;
          
        case 'd':
          /* Detach or not detach ? */
          detach_flag = TRUE ;
          break ;

        case 'R':
          /* Shall we manage  RPCSEC_GSS ? */
          fprintf( stderr, "\n\nThe -R flag is deprecated, use this syntax in the configuration file instead:\n\n" ) ;
          fprintf( stderr, "NFS_KRB5\n" ) ;
          fprintf( stderr, "{\n" ) ;
          fprintf( stderr, "\tPrincipalName = nfs@<your_host> ;\n" ) ;
          fprintf( stderr, "\tKeytabPath = /etc/krb5.keytab ;\n" ) ;
          fprintf( stderr, "\tActive_krb5 = TRUE ;\n" ) ;
          fprintf( stderr, "}\n\n\n" ) ;
          exit( 1 ) ;

          break ;

        case 'F':
          /* Flushes the data cache to the FSAL and purges the cache */	
          nfs_start_info.flush_datacache_mode = TRUE ;
          nfs_start_info.flush_behaviour      = CACHE_CONTENT_FLUSH_AND_DELETE ;
          nfs_start_info.nb_flush_threads     = (unsigned int)atoi( optarg ) ;

          if( nfs_start_info.nb_flush_threads > NB_MAX_FLUSHER_THREAD )
                nfs_start_info.nb_flush_threads = NB_MAX_FLUSHER_THREAD ;
          break ;

        case 'S':
          /* Flushes the data cache to the FSAL, without purging the cache */
          nfs_start_info.flush_datacache_mode = TRUE ;
          nfs_start_info.flush_behaviour      = CACHE_CONTENT_FLUSH_SYNC_ONLY ;
          nfs_start_info.nb_flush_threads     = (unsigned int)atoi( optarg ) ;

          if( nfs_start_info.nb_flush_threads > NB_MAX_FLUSHER_THREAD )
                nfs_start_info.nb_flush_threads = NB_MAX_FLUSHER_THREAD ;
          break ;

        case 'h':
          /* display the help */
          fprintf( stderr, usage, exec_name ) ;
          exit( 0 ) ;
          break ;
          
        case '?':
        default:
           /* ignore unsupported options*/
           fprintf( stderr, "WARNING: unknown GANESHA NFS daemon option: %c\n", (char)optopt  ) ;           
                
        }
    }
    
    
    /* initialize memory and logging */
    
    if ( nfs_prereq_init( exec_name, host_name, debug_level, log_path ) )
    {
        fprintf( stderr, "NFS MAIN: Error initializing NFSd prerequisites\n" );
        exit(1);
    }
    
    
    /* Start in background, if wanted */
    if( detach_flag )
    {
      /* Step 1: forking a service process */
      switch( son_pid = fork() )
        {
        case -1 :
          /* Fork failed */
          DisplayErrorLog( ERR_SYS, ERR_FORK, errno ) ;
          DisplayLog( "Could nout start nfs daemon, exiting..." ) ;
          exit( 1 ) ;
          
        case 0:
          /* This code is within the son (that will actually work)
           * Let's make it the leader of its group of process */
          if( setsid() == -1 )
            {
              DisplayErrorLog( ERR_SYS, ERR_SETSID, errno ) ;
              DisplayLog( "Could nout start nfs daemon, exiting..." ) ;
              exit( 1 ) ;
            }
          break ;

        default:
          /* This code is within the father, it is useless, it must die */
          DisplayLog( "Starting a son of pid %d\n", son_pid ) ;
          exit( 0 ) ;
          break ;
        }
    }
    
    DisplayLog( ">>>>>>>>>> Starting GANESHA NFS Daemon on FSAL/%s <<<<<<<<<<", FSAL_GetFSName() ) ;
    
    /* Set the signal handler */
    memset( &act_sigusr1, 0, sizeof( act_sigusr1 ) ) ;
    act_sigusr1.sa_flags = 0 ;
    act_sigusr1.sa_handler = action_sigusr1 ;
    if( sigaction( SIGUSR1, &act_sigusr1, NULL ) == -1 )
    {
        DisplayErrorLog( ERR_SYS, ERR_SIGACTION, errno ) ;
        exit( 1 ) ;
    }
    else
        DisplayLogLevel( NIV_EVENT, "Signal SIGUSR1 (force flush) is ready to be used" ) ;
        
    
    memset( &nfs_param, 0, sizeof(nfs_param) );
    
    /* initialize default parameters */
    
    if ( nfs_set_param_default( &nfs_param ) )
    {
        DisplayLog( "NFS MAIN: Error setting default parameters." );
        exit(1);
    }
    /* return all errors */
    nfs_param.core_param.drop_io_errors = FALSE;
    nfs_param.core_param.drop_inval_errors = FALSE;
        
    /* parse configuration file (if specified) */
    
    if ( strlen(config_path) > 0 )
    {
        if ( nfs_set_param_from_conf( &nfs_param, &nfs_start_info, config_path ) )
        {
          DisplayLog( "NFS MAIN: Error parsing configuration file." ) ;
          exit(1);
        }
    }
        
    /* set filesystem relative info */
    
    nfs_param.fsal_param.fs_specific_info.fs_ops = &ops;
    nfs_param.fsal_param.fs_specific_info.user_data = user_data;
    
    /* if no logfile was specified for layers, set the default logfile
     * as log descriptor */
    init_log( &nfs_param.fsal_param.fsal_info.log_outputs, log_path );
    init_log( &nfs_param.cache_layers_param.cache_inode_client_param.log_outputs, log_path );
    init_log( &nfs_param.cache_layers_param.cache_content_client_param.log_outputs, log_path );
    
#ifndef _NO_BUDDY_SYSTEM
    if ( ! nfs_param.buddy_param_worker.buddy_error_file[0] )
        strcpy( nfs_param.buddy_param_worker.buddy_error_file, log_path );
#endif
    
#ifdef _SNMP_ADM_ACTIVE
    if ( ! nfs_param.extern_param.snmp_adm.snmp_log_file[0] )
        strcpy( nfs_param.extern_param.snmp_adm.snmp_log_file, log_path );
#endif    
    
    /* add export by hand if no export was defined
     * in config file (always '/')
     */
    
    if ( ! nfs_param.pexportlist )
    {
        nfs_param.pexportlist = BuildDefaultExport();
        if ( nfs_param.pexportlist == NULL )
        {
            DisplayLog( "NFS MAIN: Could not create export entry for '/'" ) ;
            exit(1);
        }
    }
    
    
    /* if this is a single threaded application, set worker count */
    if ( single_threaded )
        nfs_param.core_param.nb_worker = 1;

    /* check parameters consitency */
    
    if ( nfs_check_param_consistency( &nfs_param ) )
    {
        DisplayLog( "NFS MAIN: Inconsistent parameters found" ) ;
        DisplayLog( "MAJOR WARNING: /!\\ | Bad Parameters could have significant impact on the daemon behavior" ) ;
        exit(1);
    }
    
    /* Everything seems to be OK! We can now start service threads */
    nfs_start( &nfs_param, &nfs_start_info );
    
    return 0;
    
}
   
