# Copy Rebuild

## Objective

Allow the reconstruction of an object's copy by relying on the extents of this
copy that are still readable.

The feature is split into two stages:

1. Rebuilding a single object's copy.
2. Rebuilding all extents on a medium.

## Store and admin API

Two new functions are introduced:

- `phobos_copy_rebuild()`
- `phobos_admin_media_rebuild()`

### Parameters

`phobos_copy_rebuild()`:

- object (`oid`, `uuid`, `version`)
- copy's name
- tags
- library

`phobos_admin_media_rebuild()`:

- a list of medium (`family`, `name`, `library`)

## CLI

### Commands

- `phobos copy rebuild <oid> <copy_name>`
- `phobos dir rebuild <dir(s)>`
- `phobos tape rebuild <tape(s)>`

### Options

#### `phobos copy rebuild`

- `--version`
- `--uuid`
- `--tags`
- `--library`

#### `phobos tape rebuild` / `phobos dir rebuild`

- `--library`

## Rebuilding of an object's copy

### Rebuilding the extents

To rebuild extents, a new `pho_data_processor` type must be added: `REBUILDER`.
From now on, the term `proc` will be used in this design to refer to the
`pho_data_processor`. This proc will be quite similar to the existing `COPIER`
type, but it will always use the same layout for both reading and writing.
This layout depends on how the extents to rebuild were originally written.

A new proc operation must be added `raid_rebuild_write_processor_step`,
along with its corresponding destroy operation.

For the read operation, we doesn't need a specific reader operation as we just
need to read the object.

The write operation will be responsible for rebuilding the missing extents from
the data read by the reader. It will also allocate the media required to write
all extents that need to be rebuilt. This allocation will request as many media
as there are extents to rebuild for the given split. These allocations will be
done with the `no_split` parameter to avoid having different numbers of splits
between the extents, as this is something Phobos doesn’t handle.

Depending on the requirements of the different layouts (`raid1` and `raid4`),
the write functions may be shared between the layouts.

Finally, the proc operation will be initialized by the `layout_rebuilder()`
function. This function will load the layout to use and initialize both the
layout itself and the write operation described above.

It is during the initialization of the write layouts that it determines which
extents must be rebuilt.

Once initialization is complete, we reuse the standard Store API mechanism
(reader / writer_step loop).

Once this loop is finished, one final operation is required: atomically updating
the new extent in the copy layout. We first add the new extent to the DSS.
Then, in the same transaction, we swap the extents in the layout table and mark
the old extent as orphan.

#### How to choose the extents to read

The extents required to rebuild the others depends on the layout being used.

For `raid1`, any valid extent can be used. For `raid4`, the two remaining valid
extents must be used. If there are not enough valid extents left, rebuilding the
others is impossible.

One step further: if there are not enough extents available to reconstruct a
copy, and another copy is at least readable, we could simply delete the partial
copy and recreate it from the second copy.

#### How to choose the extents to rebuild

When rebuilding an object copy, two situations are possible. First, some extents
may be missing from the DSS (eg. phobos tape lost). In that case, identifying
what must be rebuilt is straightforward because the missing extents are known
immediately.

Second, the extents may still be present in the DSS but no longer usable because
they are corrupted for example. This case is more difficult to handle. At the
moment, the only reliable way to detect such corruption is to read the full
extent again and compare its checksum. For obvious performance reasons,
re-reading every extent of a copy before starting the rebuild is not acceptable.

To address this, whenever Phobos reads an extent during a get or a copy create
operation and detects a checksum mismatch, it marks the extent as `ORPHAN` and
deletes it from the layout. The copy status must also be updated accordingly to
reflect that the copy is no longer fully readable or is incomplete.

This makes it much easier to determine what must be rebuilt. The rebuild
process only needs to consider extents that are missing from a copy.

The extents to rebuild will then be stored in memory in the private data of the
write layout.

#### New write operation in layouts

For rebuild, especially with raid4, a new function is needed on the write side.

The reader will read the object and put it in the proc buffer, then the writer
will reconstruct the extents from the buffer. For raid1, we just need to write
the buffer. But for raid4, the writer needs to compute what it has to write
depending on the extents it needs to rebuild. If the first half is missing, it
will retrieve the first half from the buffer and write it to the extents. The
same applies for the second half or the XOR.

To support this, a new raid write operation must be added:
`write_rebuild_from_buff`.

#### Allocate a medium in writing

It's mandatory to choose the same type of medium for the rebuild. This means the
same family, library, grouping, and tags. The only missing piece of information
is the set of tags used during the write operation.

To achieve this, we can use a best-effort approach to recover the list of tags
by computing the intersection of all tags present on all required media. This is
not perfect, because the intersection may include additional tags that were not
actually used during the put operation but happen to be present on all required
media.

The tags and library to use can be overriden with the tags and library options.

## Rebuilding a medium

To rebuild the extents of a medium, we must first schedule the extents in a way
that reduces the number of mount and unmount operations.
We must also pay attention to the order in which extents are written on tape to
avoid excessive seek.

### Scheduling

The goal of scheduling is to minimize the number of mount and unmount
operations during medium rebuild.

1. Grouping extents by medium

Before building the groups, we scan all extents to rebuild and collect the
media that may be used to rebuild each extent. At the same time, we compute the
frequency of each medium.

- raid1: the media are the possible replica media from which the extent can be
  read
- raid4: the media are the two media required to rebuild the extent

Next, we group extents according to the media required to read them back and
their frequency.

For raid1 extents, if several replica media are available, we select the
medium with the highest frequency. This favors media that are also used by other
extents.

For raid4 extents, there is no replica choice: we always choose the pair of
media.

2. Sorting extents within each group

The extents in each group are sorted by creation order to limit seeks.

3. Scheduling the groups

The objective is to keep already mounted media in use for as long as possible.

The transition cost between two groups depends on the number of media that must
be mounted or unmounted to move to the next group. The scheduling therefore
favors the next group with the lowest transition cost. This strategy reduces
media changes and therefore lowers the overall rebuild cost.

To start, we choose the group containing the most frequent medium among all
groups.

4. Tie-breaking

When several groups have the same transition cost, priority is given to the one
containing the medium that is most frequent among the remaining groups.
This increases the probability of reusing a medium that is already mounted in
subsequent steps.

If we still have several groups, we begin by the groups with the most extents
to rebuild.

#### Example

Available groups:

- raid1: {A}, {C}, {D}
- raid4: {A,B}, {A,C}, {C,D}, {D,E}

Less efficient order:

{A,B} -> {D} -> {A,C} -> {C} -> {D,E} -> {C,D}

Preferred order:

{A} -> {A,B} -> {A,C} -> {C} -> {C,D} -> {D} -> {D,E}

### Batch Allocations

One final possible optimization for media rebuild is to perform batch
allocations through the LRS. For write allocation, this is fairly
straightforward, since the goal is to rebuild all extents onto the same type
of media, much like a repack operation.

Read allocation is more complex, but it is still feasible. The main challenge
with batched read allocations is that they must be performed using objects that
require exactly the same media. In our case, this condition is satisfied by the
scheduling strategy: each group requires exactly the same media for reading.

The idea is to perform an initial batched write allocation for the total size of
all extents that need to be rebuilt. Then, for each group, perform a batched
read allocation covering all extents that must be read for rebuild.

The layouts will need to be updated to support multiple targets within a single
xfer during a read operation, somewhat similar to how mput works.
