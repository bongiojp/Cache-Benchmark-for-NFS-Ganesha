#!/usr/bin/perl

use strict;
use warnings;

use File::Basename;
use Getopt::Std;

use ConfigParsing;
use MesureTemps;
use NFS;
BEGIN {
  require NFS_tools;
  import NFS_tools qw(:DEFAULT mnt_mount3 mnt_umount3 nfs_symlink3 nfs_lookup3 nfs_getattr3 nfs_readlink3 nfs_remove3);
}

# get options
my $options = "tvs:f:m:";
my %opt;
my $usage = sprintf "Usage: %s [-t] [-v] -s <server_config> -f <config_file> [-m <mount_path>]", basename($0);
getopts( $options, \%opt ) or die "Incorrect option used\n$usage\n";
my $conf_file = $opt{f} || die "Missing Config File\n";
my $server_config = $opt{s} || die "Missing Server Config File\n";

# filehandle & path
my $roothdl = NFS::nfs_fh3::new();
my $curhdl = NFS::nfs_fh3::new();
my $rootpath = $opt{m} || "/";
my $curpath = ".";
my $flag_v = ($opt{v}) ? 1 : 0;

my $ret;

my $start = MesureTemps::Temps::new();
my $end = MesureTemps::Temps::new();

# Init layers
$ret = nfsinit( CONFIG_FILE   => $server_config,
                FLAG_VERBOSE  => $flag_v
       );
if ($ret != 0) { die "Initialization FAILED\n"; }

# mount FS
my $usemnt;
if (-e $rootpath and -f $rootpath) { # file to read to get filehandle ?
  open(FHDL, $rootpath) or die "Impossible to open $rootpath : $!\n";
  HDL: while (my $row = <FHDL>) {
    next HDL if $row =~ /^#/; # comments
    next HDL if !($row =~ /^@/); # not a file handle
    # else ..
    chomp $row;
    $ret = NFS::get_new_nfs_fh3($row, $roothdl);
    if ($ret != 0) { die ""; }
    NFS::copy_nfs_fh3($roothdl, $curhdl);
    last HDL;
  }
  $usemnt = 0;
} else {
  $ret = mnt_mount3($rootpath, \$roothdl);
  if ($ret != 0) { die "mount failed\n"; }
  NFS::copy_nfs_fh3($roothdl, $curhdl);
  $usemnt = 1;
}

# get parameters for test
my $param = ConfigParsing::readin_config($conf_file);
if (!defined($param)) { die "Nothing to built\n"; }

my $b = ConfigParsing::get_btest_args($param, $ConfigParsing::EIGHT);
if (!defined($b)) { die "Missing test number eight\n"; }

if ($b->{files} == -1) { die "Missing 'files' parameter in the config file $conf_file for the basic test number eight\n"; }
if ($b->{count} == -1) { die "Missing 'count' parameter in the config file $conf_file for the basic test number seight\n"; }

printf "%s : symlink and readlink\n", basename($0);

# get the test directory filehandle
my $testpath = ConfigParsing::get_test_directory($param);
$ret = testdir($roothdl, \$curpath, \$curhdl, $testpath);
if ($ret != 0) { die; }

# run ...
MesureTemps::StartTime($start);
for (my $ci=0; $ci<$b->{count}; $ci++) {
  for (my $fi=0; $fi<$b->{files}; $fi++) {
    # SYMLINK
    my $hdl = NFS::nfs_fh3::new();
    my $sattr = NFS::sattr3::new();
    init_sattr3(\$sattr);
    $ret = nfs_symlink3($curhdl, $b->{fname}.$fi, $sattr, $b->{sname}, \$hdl);
    if ($ret != 0) { die "can't make symlink ".$b->{fname}.$fi; }
    # GETATTR SYMLINK
    my $fattr = NFS::fattr3::new();
    $ret = nfs_getattr3($hdl, \$fattr); 
    if ($ret != 0) { die "can't stat ".$b->{fname}.$fi." after symlink\n"; }
    # FTYPE ?
    if (!NFS::is_symlink($fattr)) { die "type of ".$b->{fname}.$fi." not symlink\n"; }
    # READLINK
    my $readlinkret = NFS::READLINK3resok::new();
    $ret = nfs_readlink3($hdl, \$readlinkret);
    if ($ret != 0) { die "Readlink failed\n"; }
    if ($readlinkret->{data} ne $b->{sname}) { die "readlink ".$b->{fname}.$fi." returned '".$readlinkret->{data}."', expect '".$b->{sname}."'\n"; }
    # REMOVE
    $ret = nfs_remove3($curhdl, $b->{fname}.$fi);
    if ($ret != 0) { die "can't remove ".$b->{fname}.$fi."\n"; }
  }
}
MesureTemps::EndTime($start, $end);

# print results
print "\t".($b->{files} * $b->{count} * 2)." symlinks and readlinks on ".$b->{files}." files (size of symlink : ".(length $b->{sname}).")";
if ($opt{t}) { printf " in %s seconds", MesureTemps::ConvertiTempsChaine($end, ""); }
print "\n";

#umount
if ($usemnt) { $ret = mnt_umount3($rootpath); }

# log
my $log_file = ConfigParsing::get_log_file($param);
open(LOG, ">>$log_file") or die "Enable to open $log_file : $!\n";
printf LOG "b8\t".($b->{files} * $b->{count} * 2)."\t".$b->{files}."\t".(length $b->{sname})."\t%s\n", MesureTemps::ConvertiTempsChaine($end, "");
close LOG;

print "Basic test number eight OK ! \n";