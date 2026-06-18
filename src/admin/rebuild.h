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
 * \brief  Phobos admin header for media rebuild
 */

#ifndef _PHO_ADMIN_REBUILD_H
#define _PHO_ADMIN_REBUILD_H

#include "phobos_admin.h"
#include "pho_types.h"

#include <glib.h>

struct rebuild_extent {
    struct layout_info *layout;
    struct extent *extent_to_rebuild; /**< Extent to rebuild */
    GPtrArray *avail_extents;         /**< Extents available to rebuild from */
    int n_data_extents;               /**< Number of extents needed to rebuild
                                        */
};

struct group_key {
    int n_media;
    struct pho_id media[];
};

static inline guint group_key_hash(gconstpointer data)
{
    const struct group_key *key = data;
    guint hash = key->n_media;

    for (guint i = 0; i < key->n_media; i++)
        hash ^= g_pho_id_hash(&key->media[i]);

    return hash;
}

static inline gboolean group_key_equal(gconstpointer a, gconstpointer b)
{
    const struct group_key *ka = a;
    const struct group_key *kb = b;

    if (ka->n_media != kb->n_media)
        return false;

    for (guint i = 0; i < ka->n_media; i++)
        if (!pho_id_equal(&ka->media[i], &kb->media[i]))
            return false;

    return true;
}

/**
 * Rebuild all the extents of a copy on \p med.
 *
 * @param[in] item    the item to rebuild
 *
 * @return 0 on success, negated errno on failure
 */
int rebuild_copy(struct rebuild_extent *rebuild_extent);

/**
 * Retrieve the extents to rebuild for each copy and computes the frequency
 * of occurrence of all the media we can use to rebuild that extent.
 *
 * @param[in]     med                  The medium to rebuild
 * @param[in]     layouts              All the layouts on \p med
 * @param[in]     n_layout             The number of layouts
 * @param[in/out] frequency            Hashtable with the frequency of each
 *                                     medium
 * @param[in/out] extents_to_rebuild   The extents to rebuild
 *
 * @return 0 on success, negated errno on failure
 */
int collect_rebuild_extents_and_frequency(struct pho_id *med,
                                          struct layout_info *layouts,
                                          int n_layout,
                                          GHashTable *frequency,
                                          GArray *extents_to_rebuild);

/**
 * Group rebuild extents by the media needed to rebuild them.
 *
 * If the number of available extents is equal the n_data_extents, all
 * available media are used as the group key. Otherwise, we choose the more
 * frequent media.
 *
 * Groups are appended to \p groups.
 *
 * @param[in/out] groups               All the different groups to rebuild
 * @param[in]     extents_to_rebuild   All the extents to rebuild
 * @param[in]     frequency            Frequency of occurrence of each media
 *
 * @return 0 on success, negated errno on failure
 */
void group_extents(GHashTable *groups, GArray *extents_to_rebuild,
                   GHashTable *frequency);

#endif
