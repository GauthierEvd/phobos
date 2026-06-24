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
 * \brief  test extent list rebuild
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phobos_store.h"

static int parse_extent_idx(const char *str, int *idx)
{
    char *endptr = NULL;
    long value;

    errno = 0;
    value = strtol(str, &endptr, 10);
    if (errno || endptr == str || *endptr != '\0' ||
        value < 0 || value > INT_MAX)
        return -EINVAL;

    *idx = value;

    return 0;
}

static void set_media_read(const char *name, struct pho_id *media_read)
{
    media_read->family = PHO_RSC_DIR;
    pho_id_name_set(media_read, name, "legacy");
}

int main(int argc, char **argv)
{
    static struct option long_options[] = {
        {"media-read", required_argument, 0, 'm'},
        {0,            0,                 0,  0 },
    };
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = {0};
    struct pho_id media_read = {0};
    bool has_media_read = false;
    int *extents_idx = NULL;
    size_t n_extents = 0;
    int rc;
    char c;

    while ((c = getopt_long(argc, argv, "m:", long_options, NULL)) != -1) {
        switch (c) {
        case 'm':
            set_media_read(optarg, &media_read);
            has_media_read = true;
            break;
        default:
            fprintf(stderr, "usage: %s [--media-read name] object_id "
                            "copy_name extent_idx...\n", argv[0]);
            return -EINVAL;
        }
    }

    argv += optind;
    argc -= optind;

    if (argc < 2) {
        fprintf(stderr, "usage: %s [--media-read name] object_id copy_name "
                        "extent_idx...\n", argv[0]);
        return -EINVAL;
    }

    target.xt_objid = argv[0];
    n_extents = argc - 2;
    if (n_extents > 0)
        extents_idx = xcalloc(n_extents, sizeof(*extents_idx));

    for (size_t i = 0; i < n_extents; i++) {
        const char *extent_arg = argv[2 + i];

        rc = parse_extent_idx(extent_arg, &extents_idx[i]);
        if (rc) {
            fprintf(stderr, "invalid extent index '%s'\n", extent_arg);
            free(extents_idx);
            return -rc;
        }
    }

    xfer.xd_op = PHO_XFER_OP_REBUILD;
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.rebuild.put.copy_name = argv[1];
    xfer.xd_params.rebuild.get.copy_name = argv[1];
    xfer.xd_params.rebuild.get.scope = DSS_OBJ_ALIVE;
    xfer.xd_params.rebuild.extents_idx = n_extents > 0 ? extents_idx : NULL;
    xfer.xd_params.rebuild.n_extents = n_extents;

    if (has_media_read)
        xfer.xd_params.rebuild.media_read = pho_id_dup(&media_read);

    rc = phobos_init();
    if (rc) {
        fprintf(stderr, "phobos_init failed: %d, %s\n", rc, strerror(rc));
        free(extents_idx);
        return rc;
    }

    rc = phobos_copy_rebuild(&xfer, 1);
    if (rc)
        fprintf(stderr, "phobos_copy_rebuild failed: %s (%d)\n",
                strerror(-rc), -rc);

    pho_xfer_desc_clean(&xfer);
    phobos_fini();

    return -rc;
}
