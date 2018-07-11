#!/usr/bin/env bash
#
# Copyright (C) 2017 Red Hat <contact@redhat.com>
#
# Author: David Zafman <dzafman@redhat.com>
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

    export CEPH_MON="127.0.0.1:7180" # git grep '\<7180\>' : there must be only one
    export CEPH_ARGS
    CEPH_ARGS+="--fsid=$(uuidgen) --auth-supported=none "
    CEPH_ARGS+="--mon-host=$CEPH_MON "
    CEPH_ARGS+="--osd_min_pg_log_entries=5 --osd_max_pg_log_entries=10 "
    CEPH_ARGS+="--fake_statfs_for_testing=1228800 "
    CEPH_ARGS+="--osd_max_backfills=10 "
    export margin=10
    export objects=200
    export poolprefix=test

    local funcs=${@:-$(set | sed -n -e 's/^\(TEST_[0-9a-z_]*\) .*/\1/p')}
    for func in $funcs ; do
        setup $dir || return 1
        $func $dir || return 1
        teardown $dir || return 1
    done
}


function TEST_backfill_test_simple() {
    local dir=$1
    local pools=2
    local OSDS=3

    run_mon $dir a || return 1
    run_mgr $dir x || return 1
    export CEPH_ARGS

    for osd in $(seq 0 $(expr $OSDS - 1))
    do
      run_osd $dir $osd || return 1
    done

    for p in $(seq 1 $pools)
    do
      create_pool "${poolprefix}$p" 1 1
      ceph osd pool set "${poolprefix}$p" size 1
    done

    wait_for_clean || return 1

    # This won't work is if the 2 pools primary and only osds
    # are the same.

    dd if=/dev/urandom of=$dir/datafile bs=1024 count=4
    for o in $(seq 1 $objects)
    do
      for p in $(seq 1 $pools)
      do
	rados -p "${poolprefix}$p" put obj$o $dir/datafile
      done
    done

    for p in $(seq 1 $pools)
    do
      ceph osd pool set "${poolprefix}$p" size 2
    done
    sleep 30

    ERRORS=0
    if [ "$(ceph pg dump pgs | grep +backfill_toofull | wc -l)" != "1" ];
    then
      echo "One pool should have been in backfill_toofull"
      ERRORS="$(expr $ERRORS + 1)"
    fi

    if [ "$(ceph pg dump pgs | grep active+clean | wc -l)" != "1" ];
    then
      echo "One didn't finish backfill"
      ERRORS="$(expr $ERRORS + 1)"
    fi

    ceph pg dump pgs

    if [ $ERRORS != "0" ];
    then
      return 1
    fi

    for i in $(seq 1 $pools)
    do
      delete_pool "${poolprefix}$i"
    done
    kill_daemons $dir || return 1
}

function TEST_backfill_test_multi() {
    local dir=$1
    local pools=4
    local OSDS=10

    run_mon $dir a || return 1
    run_mgr $dir x || return 1
    export CEPH_ARGS

    for osd in $(seq 0 $(expr $OSDS - 1))
    do
      run_osd $dir $osd || return 1
    done

    for p in $(seq 1 $pools)
    do
      create_pool "${poolprefix}$p" 1 1
      ceph osd pool set "${poolprefix}$p" size 1
    done

    wait_for_clean || return 1

    dd if=/dev/urandom of=$dir/datafile bs=1024 count=4
    for o in $(seq 1 $objects)
    do
      for p in $(seq 1 $pools)
      do
	rados -p "${poolprefix}$p" put obj$o $dir/datafile
      done
    done

    for p in $(seq 1 $pools)
    do
      ceph osd pool set "${poolprefix}$p" size 2
    done
    sleep 30

    ERRORS=0
    #if [ "$(ceph pg dump pgs | grep +backfill_toofull | wc -l)" != "1" ];
    #then
      #echo "One pool should have been in backfill_toofull"
      #ERRORS="$(expr $ERRORS + 1)"
    #fi

    #if [ "$(ceph pg dump pgs | grep active+clean | wc -l)" != "1" ];
    #then
      #echo "One didn't finish backfill"
      #ERRORS="$(expr $ERRORS + 1)"
    #fi

    ceph pg dump pgs

    if [ $ERRORS != "0" ];
    then
      return 1
    fi

    for i in $(seq 1 $pools)
    do
      delete_pool "${poolprefix}$i"
    done
    kill_daemons $dir || return 1
}



function TEST_backfill_test_sametarget() {
    local dir=$1
    local pool2="${poolname}2"

    run_mon $dir a || return 1
    run_mgr $dir x || return 1
    export CEPH_ARGS
    run_osd $dir 0 || return 1
    run_osd $dir 1 || return 1
    run_osd $dir 2 || return 1

    ceph osd set-require-min-compat-client luminous

    create_pool $poolname 1 1
    ceph osd pool set $poolname size 1

    create_pool $pool2 1 1
    ceph osd pool set $pool2 size 1

    wait_for_clean || return 1

    local PG1=$(get_pg $poolname obj1)
    local PG2=$(get_pg $pool2 obj1)

    ceph osd pg-upmap-items $PG1 0 2
    ceph osd pg-upmap-items $PG2 2 1

    wait_for_clean || return 1

    dd if=/dev/urandom of=$dir/datafile bs=1024 count=4
    for i in $(seq 1 $objects)
    do
	rados -p $poolname put obj$i $dir/datafile
	rados -p $pool2 put obj$i $dir/datafile
    done

    ceph osd pool set $poolname size 2
    ceph osd pool set $pool2 size 2
    sleep 30

    ERRORS=0
    if [ "$(ceph pg dump pgs | grep +backfill_toofull | wc -l)" != "1" ];
    then
      echo "One pool should have been in backfill_toofull"
      ERRORS="$(expr $ERRORS + 1)"
    fi

    if [ "$(ceph pg dump pgs | grep active+clean | wc -l)" != "1" ];
    then
      echo "One didn't finish backfill"
      ERRORS="$(expr $ERRORS + 1)"
    fi

    ceph pg dump pgs

    if [ $ERRORS != "0" ];
    then
      return 1
    fi

    delete_pool $poolname
    kill_daemons $dir || return 1
}



main osd-backfill-space "$@"

# Local Variables:
# compile-command: "make -j4 && ../qa/run-standalone.sh osd-backfill-space.sh"
# End:
