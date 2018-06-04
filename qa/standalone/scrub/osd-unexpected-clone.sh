#!/usr/bin/env bash
#
# Copyright (C) 2015 Intel <contact@intel.com.com>
# Copyright (C) 2014, 2015 Red Hat <contact@redhat.com>
#
# Author: Xiaoxi Chen <xiaoxi.chen@intel.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Library Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Library Public License for more details.
#

source $CEPH_ROOT/qa/standalone/ceph-helpers.sh

function run() {
    local dir=$1
    shift

    export CEPH_MON="127.0.0.1:7144" # git grep '\<7144\>' : there must be only one
    export CEPH_ARGS
    CEPH_ARGS+="--fsid=$(uuidgen) --auth-supported=none "
    CEPH_ARGS+="--mon-host=$CEPH_MON "
    CEPH_ARGS+="--osd-objectstore-fuse "

    local funcs=${@:-$(set | sed -n -e 's/^\(TEST_[0-9a-z_]*\) .*/\1/p')}
    for func in $funcs ; do
        setup $dir || return 1
        $func $dir || return 1
        teardown $dir || return 1
    done
}

function TEST_recover_unexpected() {
    local dir=$1

    run_mon $dir a || return 1
    run_mgr $dir x || return 1
    run_osd $dir 0 || return 1
    run_osd $dir 1 || return 1
    run_osd $dir 2 || return 1

    ceph osd pool create foo 1
    rados -p foo put foo /etc/passwd
    rados -p foo mksnap snap
    rados -p foo put foo /etc/motd

    cp $dir/1/fuse/1.0_head/all/#1\:602f83fe\:\:\:foo\:1#/attr/_ _
    cp $dir/1/fuse/1.0_head/all/#1\:602f83fe\:\:\:foo\:1#/data data

    rados -p foo rmsnap snap

    sleep 5

    primary=1
    mkdir $dir/$primary/fuse/1.0_head/all/#1\:602f83fe\:\:\:foo\:1#
    cat data > $dir/$primary/fuse/1.0_head/all/#1\:602f83fe\:\:\:foo\:1#/data
    cat _ > $dir/$primary/fuse/1.0_head/all/#1\:602f83fe\:\:\:foo\:1#/attr/_

    sleep 5

    ceph pg scrub 1.0
    ceph pg repair 1.0

    sleep 10

    # make sure osds are still up
    ceph tell osd.0 version
    ceph tell osd.1 version
    ceph tell osd.2 version
}


main osd-unexpected-clone "$@"

# Local Variables:
# compile-command: "cd ../.. ; make -j4 && test/osd/osd-bench.sh"
# End:
