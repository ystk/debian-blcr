#! /bin/sh 
#
#   Berkeley Lab Checkpoint/Restart (BLCR) for Linux is Copyright (c)
#   2008, The Regents of the University of California, through Lawrence
#   Berkeley National Laboratory (subject to receipt of any required
#   approvals from the U.S. Dept. of Energy).  All rights reserved.
#
#   Portions may be copyrighted by others, as may be noted in specific
#   copyright notices within specific files.
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
#  $Id: autogen.sh,v 1.7 2008/04/29 18:53:37 phargrov Exp $

set -e

mkdir -p config
rm -f {.,config}/{config.guess,config.sub,install-sh,libtool,ltmain.sh,missing,mkinstalldirs}
rm -rf autom4te.cache
aclocal
autoheader
autoconf
libtoolize --automake --copy
if [ -f ./ltmain.sh ]; then
	# older libtool didn't find AC_CONFIG_AUX_DIR in configure.ac
	mv ltmain.sh config/
	rm -f config.sub config.guess
fi
automake --include-deps --add-missing --copy

if [ -x config.status ]; then
    echo "################################################################"
    echo "# You appear to have already configured the build environment. #"
    echo "# Trying to update your configuration.                         #"
    echo "################################################################"
    ./config.status
    make clean
fi

