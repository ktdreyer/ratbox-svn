#!/usr/bin/perl -w
#
# generate-schema.pl
# Replaces all the string values in a given schema with the actual numeric
# values, taken from headers.
#
# Copyright (C) 2006-2008 Lee Hardy <lee@leeh.co.uk>
# Copyright (C) 2006-2008 ircd-ratbox development team
#
# This code is in the public domain.

require "definetolength.pl";
use File::Basename;

my @schemas = ("base/schema-mysql.txt", "base/schema-pgsql.txt");

my %vals = &parse_includes("../include");

unless(-r "../include/setup.h")
{
	print("Unable to read ../include/setup.h, please run configure first\n");
	exit();
}

if($ARGV[0])
{
	@schemas = ("$ARGV[0]");
}

foreach my $i (@schemas)
{
	my $outputfile = basename($i);

	unless(open(INPUT, '<', "$i"))
	{
		print("Unable to open base schema $i for reading, aborted.\n");
		exit();
	}

	local $/ = undef;
	my $input = <INPUT>;

	unless(open(OUTPUT, '>', "$outputfile"))
	{
		print("Unable to open schema $i for writing, aborted.\n");
		exit();
	}

	while(($key, $value) = each(%vals))
	{
		if($value == 1)
		{
			print("Unable to set $key -- not found.\n");
			next;
		}

		$input =~ s/$key/$value/g;
	}

	# this 
	$special = $vals{"NICKLEN"} + $vals{"USERLEN"} + $vals{"HOSTLEN"} + 2;
	$input =~ s/CONVERT_NICK_USER_HOST/$special/g;

	print OUTPUT "$input";
}

