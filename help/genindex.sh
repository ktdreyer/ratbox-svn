#!/bin/sh
#
# This code builds two files per directory, index and index-admin.
# It takes the first word of each file in each directory, pads it
# to 10 spaces and adds the second line of the file to it.
#
# If the second line is marked [ADMIN] it goes in index-admin
# index first gets the contents of the "general" file.

SUBDIRS="alis operbot chanserv userserv jupeserv operserv nickserv global banserv watchserv"

for i in $SUBDIRS; do
	rm -f $i/index;
	rm -f $i/index-admin;

	cp $i/general $i/index

	for j in $i/*; do
		if [ -f $j ]; then
			foo=`basename "$j"`;

			# if it starts with "u-" then its ucommand and we
			# dont care about it
			case "$foo" in
				u-*) continue;;
				index|index-admin|general) continue;;
			esac

			arg1=`head -n 1 $j | cut -d ' ' -f 1`;
			arg2=`head -n 2 $j | tail -n 1`;

			admin=`echo "$arg2" | cut -d ' ' -f 1`;

			if [ "$admin" = "[ADMIN]" ]; then
				admin=`echo "$arg2" | cut -d ' ' -f 2-`;
				printf " %-13s - %s\n" $arg1 "$admin" >> $i/index-admin;
			else
				printf " %-11s - %s\n" $arg1 "$arg2" >> $i/index;
			fi
		fi
	done
done
