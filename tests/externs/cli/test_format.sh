#!/bin/bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the License, or
#  (at your option) any later version.
#
#  Phobos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
#

# set python and phobos environment
test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../tape_drive.sh
lrs_simple_client="$test_dir/lrs_simple_client"

set -xe

function setup
{
    setup_tables
    invoke_daemon
}

function cleanup
{
    waive_daemon
    drop_tables
    if [[ -w /dev/changer ]]; then
        drain_all_drives
    fi
}

function execute_test_double_format
{
    local dir_or_tape=$1
    local medium=$2

    if [[ ${dir_or_tape} == "dir" ]]; then
        local phobos_format_options="dir format --unlock"
    else
        local phobos_format_options="tape format --unlock"
    fi

    set +e
    $valg_phobos ${phobos_format_options} ${medium} &
    local format_pid_1=$!
    $valg_phobos ${phobos_format_options} ${medium}
    local format_status_2=$?
    wait ${format_pid_1}
    local format_status_1=$?
    set -e
    if (( ! ( ${format_status_1} == 0 ^ ${format_status_2} == 0 ) )); then
        error "One of two concurrent ${dir_or_tape} formats should" \
              "have succeeded and the other should have failed, got" \
              " ${format_status_1} and ${format_status_2}."
    fi
}

function test_double_format
{
    # Trying to concurrently format the same dir
    local dir=$(mktemp -d /tmp/test_double_format.XXXX)
    $phobos dir add ${dir}
    set +e
    execute_test_double_format dir ${dir}

    if [[ -w /dev/changer ]]; then
        # Trying to concurrently format the same tape
        local tape=$(get_tapes L6 1)
        $phobos tape add --type lto6 ${tape}
        local drive=$(get_lto_drives 6 1)
        $phobos drive add --unlock ${drive}
        execute_test_double_format tape ${tape}
    fi
}

function test_eagain_format
{
    local two_lto6_tapes=( $(get_tapes L6 2 | nodeset -e) )
    local lto6_drive=$(get_lto_drives 6 1)

    # prepare one drive and one tape
    $phobos tape add --type lto6 ${two_lto6_tapes[0]}
    $phobos drive add --unlock ${lto6_drive}
    $valg_phobos tape format --unlock ${two_lto6_tapes[0]}

    # One put to busy the device
    local release_medium_name=$($lrs_simple_client put tape)

    # Try to format a busy drive
    $phobos tape add --type lto6 ${two_lto6_tapes[1]}
    $valg_phobos tape format --unlock ${two_lto6_tapes[1]} &
    local format_pid=$!

    # Be sure the format request comes to the lrs
    sleep 1

    # Release the put request
    $lrs_simple_client release $release_medium_name tape

    # Check format successfully end
    wait ${format_pid}
}

function assert_formated
{
    echo "$1" | grep "$2" | grep "empty" || error "'$2' should be formated"
}

function test_force_format
{
    local tape=$(get_tapes L6 1)
    local drive=$(get_lto_drives 6 1)

    $phobos tape add --type lto6 ${tape}
    $phobos drive add --unlock ${drive}
    $phobos tape format --unlock ${tape}

    $phobos put /etc/hosts forced_obj1
    trap "rm -f tmp; cleanup" EXIT
    $phobos get forced_obj1 tmp ||
        error "Getting forced_obj1 before format should have succeeded"
    rm tmp

    $phobos tape format ${tape} &&
        error "Formatting without force should have failed"
    $valg_phobos tape format --force ${tape}
    local output="$($phobos tape list -o all)"
    assert_formated "$output" "${tape}"

    $phobos put /etc/hosts forced_obj2
    $phobos get forced_obj1 tmp &&
        error "Getting forced_obj1 after format should have failed"
    $phobos get forced_obj2 tmp ||
        error "Getting forced_obj2 should have succeeded"
    rm tmp
    trap cleanup EXIT
}

function format_check_selected_device
{
    local tape=$1
    local targeted_drive=$2

    $valg_phobos tape format --unlock  ${tape}
    local loaded_tape=$($phobos drive status -o name,media | \
                        grep ${targeted_drive} | cut -d '|' -f 3 | \
                        cut -d ' ' -f 2)
    if [[ ${loaded_tape} == ${tape} ]]; then
        return 0
    else
        return 1
    fi
}

function test_drive_selection_format
{
    local four_lto6_tapes=( $(get_tapes L6 4 | nodeset -e) )
    local three_lto6_drives=( $(get_lto_drives 6 3) )

    # start with one drive with one mounted medium
    $phobos tape add --type lto6 ${four_lto6_tapes[0]}
    $phobos drive add --unlock ${three_lto6_drives[0]}
    $valg_phobos tape format --unlock ${four_lto6_tapes[0]}
    # Put an object to mount the first medium
    local file_to_put=$(mktemp /tmp/file_to_put.XXXX)
    dd if=/dev/urandom of=${file_to_put} bs=1k count=1
    $phobos put -f tape ${file_to_put} first_obj

    # Add a new tape and a new drive
    $phobos tape add --type lto6 ${four_lto6_tapes[1]}
    $phobos drive add --unlock ${three_lto6_drives[1]}

    # Check empty drive is chosen to format instead of mounted one
    if ! format_check_selected_device ${four_lto6_tapes[1]} \
                                      ${three_lto6_drives[1]}; then
        error "New format should have happened in the empty drive, not the" \
              "mounted one"
    fi

    # Add a new tape
    $phobos tape add --type lto6 ${four_lto6_tapes[2]}

    # Check loaded drive is chosen to format instead of mounted one
    if ! format_check_selected_device ${four_lto6_tapes[2]} \
                                      ${three_lto6_drives[1]}; then
        error "New format should have happened in the loaded drive, not the" \
              "mounted one"
    fi

    # Add a new tape and a new drive
    $phobos tape add --type lto6 ${four_lto6_tapes[3]}
    $phobos drive add --unlock ${three_lto6_drives[2]}

    # Check empty drive is chosen instead of loaded one
    if ! format_check_selected_device ${four_lto6_tapes[3]} \
                                      ${three_lto6_drives[2]}; then
        error "New format should have happened in the empty drive, not the" \
              "mounted or loaded ones"
    fi
}

function test_multiple_formats
{
    local dirs=($(mktemp -d /tmp/test.pho.XXXX) $(mktemp -d /tmp/test.pho.XXXX))
    trap "rm -rf ${dirs[0]} ${dirs[1]}; cleanup" EXIT

    $phobos dir add ${dirs[@]}

    # Test formatting 2 valid dirs return no error
    $valg_phobos dir format --unlock ${dirs[@]} ||
        error "Formatting 2 valid directories '$dirs' should have succeeded"

    local output=$($phobos dir list -o all)
    assert_formated "$output" "${dirs[0]}"
    assert_formated "$output" "${dirs[1]}"

    dirs+=($(mktemp -d /tmp/test.pho.XXXX))
    trap "rm -rf ${dirs[0]} ${dirs[1]} ${dirs[2]}; cleanup" EXIT

    # Test formatting 3 dirs with 2 already formatted and 1 non-existing
    $valg_phobos dir format --unlock ${dirs[@]} &&
        error "Format with already formatted dirs and a non-existing dir" \
              "should have failed"

    local output=$($phobos dir list -o all)
    assert_formated "$output" "${dirs[0]}"
    assert_formated "$output" "${dirs[1]}"
    echo "$output" | grep "${dirs[2]}" &&
        error "'${dirs[2]}' shouldn't be listed"

    $phobos dir add ${dirs[2]}

    # Test formatting 3 dirs with 2 already formatted
    $valg_phobos dir format --unlock ${dirs[@]} &&
        error "Format with already formatted dirs should have failed"

    local output="$($phobos dir list -o all)"
    assert_formated "$output" "${dirs[0]}"
    assert_formated "$output" "${dirs[1]}"
    assert_formated "$output" "${dirs[2]}"

    # Test force formatting a dir already formatted
    # TODO: Deactivated until force formatting a directory is allowed
#    $valg_phobos dir format --force ${dirs[0]} ||
#        error "Force formatting 1 already formatted dir should have succeeded"
#
#    local output="$($phobos dir list -o all)"
#    assert_formated "$output" "${dirs[0]}"

    rm -rf ${dirs[@]}
    trap cleanup EXIT
}

function test_format_nb_streams_loop
{
    local nb_dirs=$1
    local nb_streams=$2
    local done=0
    local cnt=0

    shift 2
    local dirs=($@)

    # Test formatting $nb_dirs dirs but only $nb_streams at a time
    local output=$($valg_phobos -vv dir format --nb-streams $nb_streams \
                                               --unlock ${dirs[@]} 2>&1) ||
        error "Formatting $nb_dirs dirs $nb_streams at a time should have" \
              "succeeded"

    while IFS= read -r line
    do
        if echo "$line" | grep "pho_comm_send" | grep "Sending"; then
            cnt=$(( cnt + 1 ))
            if (( cnt > nb_streams )); then
                error "More than $nb_streams concurrent format ongoing"
            fi
        fi

        if echo "$line" | grep "_recv_client" | grep "Received"; then
            cnt=$(( cnt - 1 ))
            done=$(( done + 1 ))
        fi
    done < <(echo "$output")

    if (( done != nb_dirs )); then
        error "Not all $nb_dirs dir format ended. Done: $done"
    fi

}

function test_format_nb_streams
{
    local dirs1=(
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
    )
    local dirs2=(
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
    )
    local dirs3=(
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
        $(mktemp -d /tmp/test.pho.XXXX)
    )

    trap "rm -rf ${dirs1[0]} ${dirs1[1]};
          rm -rf ${dirs2[0]} ${dirs2[1]} ${dirs2[2]} ${dirs2[3]} ${dirs2[4]};
          cleanup" EXIT

    $phobos dir add ${dirs1[@]} ${dirs2[@]}

    test_format_nb_streams_loop 2 3 ${dirs1[@]}

    test_format_nb_streams_loop 5 2 ${dirs2[@]}

    test_format_nb_streams_loop 3 1 ${dirs3[@]}

    rm -rf ${dirs1[@]} ${dirs2[@]}
    trap cleanup EXIT
}

trap cleanup EXIT

setup
test_double_format
cleanup
setup
test_multiple_formats
cleanup
setup
test_format_nb_streams

if [[ -w /dev/changer ]]; then
    cleanup
    setup
    test_eagain_format
    cleanup
    setup
    test_drive_selection_format
    cleanup
    setup
    test_force_format
fi
