function setup_test_dirs
{
    DIR_TEST="/tmp/$(basename -s .sh ${BASH_SOURCE[1]})"
    DIR_TEST_IN="$DIR_TEST/in"
    DIR_TEST_OUT="$DIR_TEST/out"

    mkdir -p $DIR_TEST $DIR_TEST_IN $DIR_TEST_OUT
}

function cleanup_test_dirs
{
    rm -rf $DIR_TEST
}

function setup_dummy_files
{
    local nb_files=$1
    local bs=$2
    local count=$3
    local i=0

    FILES=()

    set +x
    # do not display file creation commands to reduce the amount of logs
    while [ $i -lt $nb_files ]
    do
        FILES+=("$(mktemp $DIR_TEST_IN/XXXXXX)")
        dd if=/dev/urandom of=${FILES[$i]} bs=${bs} count=${count} &>/dev/null
        i=$((i+1))
    done
    set -x
}

function cleanup_dummy_files
{
    rm -f $FILES
}

function generate_prefix_id
{
    echo "$(basename -s .sh ${BASH_SOURCE[1]})/${FUNCNAME[1]}"
}

function make_tmp_fs()
{
    local mount_point=$(mktemp -d)
    local file_path=$(mktemp)
    local loop_device
    local unit=${1: -1} # Last char
    local count=${1::-1} # All the string except the last char

    [[ $unit =~ [kKmMgG] ]] ||
        error "Invalid unit $unit"

    if (( count < 60)) && [[ $unit == k || $unit == K ]]; then
        error "Ext4 size should be at least 60K"
    fi

    dd if=/dev/zero of=$file_path count=$count bs=1$unit 2>/dev/null

    set +e
    loop_device=$(losetup -f)
    local rc=$?
    set -e

    if ((rc != 0)); then
        skip
    fi

    losetup $loop_device $file_path

    sectors=$(blockdev --getsz $loop_device)
    dm_name="tmpfs_$(basename $mount_point)"
    fs_device="/dev/mapper/$dm_name"

    dmsetup create "$dm_name" --table "0 $sectors linear $loop_device 0"

    printf '%s\n%s\n' "$loop_device" "$file_path" > "/tmp/${dm_name}.state"

    mkfs.ext4 $fs_device >/dev/null 2>&1

    mkdir -p $mount_point
    mount $fs_device $mount_point

    echo $mount_point
}

function make_tmp_fs_fail_io()
{
    local mount_point=$1
    local fs_device
    local dm_name
    local sectors

    fs_device=$(findmnt -n -o SOURCE --target "$mount_point")
    dm_name=$(dmsetup info -C --noheadings -o name "$fs_device" |
              awk '{$1=$1; print}')
    sectors=$(blockdev --getsz "$fs_device")

    echo 3 > /proc/sys/vm/drop_caches

    dmsetup suspend "$dm_name"
    dmsetup reload "$dm_name" --table "0 $sectors error"
    dmsetup resume "$dm_name"
}

cleanup_tmp_fs()
{
    local mount_point=$1
    local loop_device=$(df $mount_point | tail -n 1 | awk '{print $1}')
    local back_file

    fs_device=$(findmnt -n -o SOURCE --target "$mount_point")
    dm_name=$(dmsetup info -C --noheadings -o name "$fs_device" |
              awk '{$1=$1; print}')
    state_file="/tmp/${dm_name}.state"

    loop_device=$(sed -n '1p' "$state_file")
    back_file=$(sed -n '2p' "$state_file")

    umount $mount_point
    rmdir $mount_point

    dmsetup remove "$dm_name"
    losetup -d $loop_device
    rm -f $back_file
    rm -f $state_file
}
