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

my %lengths = (
	"USERREGNAME_LEN" => 1,
	"PASSWDLEN" => 1,
	"EMAILLEN" => 1,
	"OPERNAMELEN" => 1,
	"NICKLEN" => 1,
	"USERLEN" => 1,
	"CHANNELLEN" => 1,
	"TOPICLEN" => 1,
	"HOSTLEN" => 1,
	"REALLEN" => 1,
	"REASONLEN" => 1
);

my @srcs = ("setup.h", "rserv.h", "channel.h", "client.h");

my @schemas = ("schema-mysql.txt", "schema-pgsql.txt");

foreach my $i (@srcs)
{
	unless(open(INPUT, '<', "../include/$i"))
	{
		next;
	}

	while(<INPUT>)
	{
		chomp;

		if($_ =~ /^#define ([A-Z_]+)\s+\(?(\d+)/)
		{
			$key = $1;
			$value = $2;

			$lengths{"$key"} = $value
				if($lengths{"$key"});
		}
	}

	close(INPUT);
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

	while(($key, $value) = each(%lengths))
	{
		if($value == 1)
		{
			print("Unable to set $key -- not found.\n");
			next;
		}

		$input =~ s/$key/$value/g;
	}

	# this 
	$special = $lengths{"NICKLEN"} + $lengths{"USERLEN"} + $lengths{"HOSTLEN"} + 2;
	$input =~ s/CONVERT_NICK_USER_HOST/$special/g;

	print OUTPUT "$input";
}
