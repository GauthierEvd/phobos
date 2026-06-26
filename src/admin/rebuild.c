/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  Phobos admin source file for media rebuild
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_store.h"

#include "pho_common.h"
#include "pho_type_utils.h"
#include "pho_types.h"
#include "pho_layout.h"

#include "rebuild.h"

int rebuild_copy(struct rebuild_extent *rebuild_extent)
{
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = {0};
    int *extents_idx;
    int rc = 0;

    target.xt_objid = rebuild_extent->layout->oid;
    target.xt_objuuid = rebuild_extent->layout->uuid;
    target.xt_version = rebuild_extent->layout->version;

    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.rebuild.put.copy_name = rebuild_extent->layout->copy_name;
    xfer.xd_params.rebuild.get.copy_name = rebuild_extent->layout->copy_name;
    xfer.xd_params.rebuild.get.scope = DSS_OBJ_ALL;

    xfer.xd_params.rebuild.n_extents = 1;

    extents_idx = xmalloc(sizeof(*extents_idx));
    extents_idx[0] = rebuild_extent->extent_to_rebuild->layout_idx;
    xfer.xd_params.rebuild.extents_idx = extents_idx;

    rc = phobos_copy_rebuild(&xfer, 1);
    if (rc)
        pho_error(rc, "Failed to rebuild copy");

    pho_xfer_desc_clean(&xfer);

    return rc;
}

static int rebuild_extent_init(struct rebuild_extent *rebuild_extent,
                               struct layout_info *layout,
                               struct extent *extent_to_rebuild)
{
    int n_parity_extents;
    int rc;

    memset(rebuild_extent, 0, sizeof(*rebuild_extent));

    rebuild_extent->layout = layout;
    rebuild_extent->avail_extents = g_ptr_array_new();
    rebuild_extent->extent_to_rebuild = extent_to_rebuild;

    rc = layout_get_replica_info(layout, &rebuild_extent->n_data_extents,
                                 &n_parity_extents);
    if (rc)
        return rc;

    rebuild_extent->avail_extents =
        layout_get_extents_to_rebuild_from(layout, extent_to_rebuild);

    if (rebuild_extent->avail_extents == NULL)
        return -EINVAL;

    return 0;
}

static void compute_frequency(GPtrArray *avail_extents, GHashTable *frequency)
{
    int freq;

    for (int i = 0; i < avail_extents->len; i++) {
            struct extent *extent =
                (struct extent *) g_ptr_array_index(avail_extents, i);

            freq = GPOINTER_TO_INT(g_hash_table_lookup(frequency,
                                                       &extent->media));
            g_hash_table_replace(frequency, pho_id_dup(&extent->media),
                                 GINT_TO_POINTER(freq + 1));
    }
}

int collect_rebuild_extents_and_frequency(struct pho_id *med,
                                          struct layout_info *layouts,
                                          int n_layout, GHashTable *frequency,
                                          GArray *extents_to_rebuild)
{
    int rc;

    for (int i = 0; i < n_layout; i++) {
        struct layout_info *layout = &layouts[i];

        for (int j = 0; j < layout->ext_count; j++) {
            struct extent *extent = &layout->extents[j];
            struct rebuild_extent rebuild_extent;

            if (!pho_id_equal(med, &extent->media))
                continue;

            rc = rebuild_extent_init(&rebuild_extent, layout, extent);
            if (rc)
                return rc;

            compute_frequency(rebuild_extent.avail_extents, frequency);
            g_array_append_val(extents_to_rebuild, rebuild_extent);
        }
    }

    return 0;
}
