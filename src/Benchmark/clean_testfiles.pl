#!/usr/bin/perl


$rootpath = $ARGV[0];
$numdir = $ARGV[1];
$numfiles = $ARGV[2];

foreach( 0 .. $numdir - 1) {
    $testdir = $_;
    foreach( 0 .. $numfiles - 1) {
	$testfile = $_;
	`rm ${rootpath}/${testdir}/${testfile}`;
    }
    `rmdir ${rootpath}/${testdir}`;
}
