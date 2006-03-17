#!/usr/bin/perl -w
#
# dbupgrade.pl
# This script generates the SQL commands for the database alterations needed
# when upgrading versions of ratbox-services.
#
# It takes the version of ratbox-services as an argument, eg:
#   ./dbupgrade.pl ratbox-services-1.0.3
# You may leave off the "ratbox-services-" if you wish.  You should NOT
# however leave off extra version information like "rc1".
#
# Note, this script will only deal with actual releases, not svn copies.
# 
# Copyright (C) 2006 Lee Hardy <lee -at- leeh.co.uk>
# Copyright (C) 2006 ircd-ratbox development team
#
# $Id$

use strict;

require "definetolength.pl";

unless($ARGV[0] && $ARGV[1])
{
	print "Usage: dbupgrade.pl <ratbox-services version> <sqlite|mysql|pgsql> [include_path]\n";
	print "Eg, ./dbupgrade.pl 1.0.3 sqlite\n";
	exit;
}

my %versionlist = (
	"1.0.0"		=> 1,
	"1.0.1"		=> 1,
	"1.0.2"		=> 1,
	"1.0.3"		=> 1
);

my $version = $ARGV[0];
my $dbtype = $ARGV[1];
my %vals;

$version =~ s/^ircd-ratbox-//;

my $currentver = $versionlist{"$version"};

if(!$currentver)
{
	print "Unknown version $version\n";
	exit;
}

if($dbtype ne "sqlite" && $dbtype ne "mysql" && $dbtype ne "pgsql")
{
	print "Unknown database type $dbtype\n";
	exit;
}

if($ARGV[2])
{
	%vals = &parse_includes("$ARGV[2]");
}
else
{
	%vals = &parse_includes("../include");
}

while(my ($key, $value) = each(%vals))
{
	if($value == 1)
	{
		print "Unable to set $key -- include path must be wrong.\n";
		exit;
	}
}

if($currentver < 2)
{
	print "-- To version 1.1.0beta1\n";

	if($dbtype eq "sqlite")
	{
		print "CREATE TABLE users_resetpass (\n";
		print "    username TEXT, token TEXT, time INTEGER,\n";
		print "    PRIMARY KEY(username)\n";
		print ");\n";
		print "CREATE TABLE users_sync (\n";
		print "    id INTEGER PRIMARY KEY, hook TEXT, data TEXT\n";
		print ");\n";
		print "ALTER TABLE users ADD COLUMN verify_token TEXT;\n";
	}
	elsif($dbtype eq "mysql")
	{
		print "CREATE TABLE users_sync (\n";
		print "    id INTEGER AUTO_INCREMENT, hook VARCHAR(50) NOT NULL, data TEXT,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
	}
	else
	{
		print "CREATE TABLE users_sync (\n";
		print "    id SERIAL, hook VARCHAR(50) NOT NULL, data TEXT,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
	}

	if($dbtype eq "mysql" || $dbtype eq "pgsql")
	{
		print "CREATE TABLE users_resetpass (\n";
		print "    username VARCHAR(".$vals{"USERREGNAME_LEN"}.") NOT NULL, token VARCHAR(10), time INTEGER,\n";
		print "    PRIMARY KEY(username)\n";
		print ");\n";
		print "ALTER TABLE users ADD COLUMN verify_token VARCHAR(8);\n";
	}


	print "CREATE TABLE global_welcome (\n";
	print "    id INTEGER, text TEXT,\n";
	print "    PRIMARY KEY(id)\n";
	print ");\n";
	print "UPDATE operbans SET mask=LOWER(mask) WHERE 1;\n";

	print "\n";
}

exit;