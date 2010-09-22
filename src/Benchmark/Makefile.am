#AM_CFLAGS               = $(FSAL_CFLAGS) $(SEC_CFLAGS) -L/usr/lib64 -lglib-2.0 -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include 
AM_CFLAGS               = $(FSAL_CFLAGS) $(SEC_CFLAGS) -lglib-2.0 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include 
bin_PROGRAMS            = benchmark
benchmark_SOURCES       = benchmark.c                      \
                          ../include/cache_inode.h         \
                          ../include/fsal.h                \
                          ../include/fsal_types.h          \
                          ../include/HashData.h            \
                          ../include/HashTable.h           \
                          ../MainNFSD/nfs_init.h

benchmark_LDADD         = ../MainNFSD/libMainServices.la  
#benchmark_LDADD         = ../MainNFSD/libMainServices.la  \
#                          ../snmp_adm/libsnmp_adm.la
new: clean all
