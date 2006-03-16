#!/usr/bin/perl -w
#
# generate-schema.pl
# Replaces all the string values in a given schema with the actual numeric
# values, taken from headers.
#
# Copyright (C) 2006 Lee Hardy <lee -at- leeh.co.uk>
# Copyright (C) 2006 ircd-ratbox development team
#
# This code is in the public domain.

require "../tools/definetolength.pl";

my @schemas = ("schema-mysql.txt", "schema-pgsql.txt");

my %vals;

if($ARGV[0])
{
	%vals = &parse_includes("$ARGV[0]");
}
else
{
	%vals = &parse_includes("../include");
}

foreach my $i (@schemas)
{
	unless(open(INPUT, '<', "base/$i"))
	{
		print("Unable to open base schema base/$i for reading, aborted.\n");
		exit();
	}

	local $/ = undef;
	my $input = <INPUT>;

	unless(open(OUTPUT, '>', "$i"))
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
