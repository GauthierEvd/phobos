/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * All rights reserved (c) 2014-2022 CEA/DAM.
 *
 * This file is part of Phobos.
 *
 * Phobos is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Phobos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief Test delete API call
 */
#include <stdlib.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_cfg.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_test_utils.h"
#include "pho_type_utils.h"
#include "phobos_store.h"

#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))

static bool test_delete_null_list(void)
{
    int rc;

    rc = phobos_delete(NULL, 0);
    if (rc)
        return false;

    return true;
}

static bool test_delete_success(void)
{
    struct pho_xfer_target targets[] = {
        { .xt_objid = "test-oid1" },
        { .xt_objid = "test-oid2" },
        { .xt_objid = "test-oid3" }
    };
    struct pho_xfer_desc xfers[] = {
        { .xd_ntargets = 1,
          .xd_targets = &targets[0],
        },
        { .xd_ntargets = 1,
          .xd_targets = &targets[1],
        },
        { .xd_ntargets = 1,
          .xd_targets = &targets[2],
        }
    };
    int rc;

    /* process the first xfer element */
    rc = phobos_delete(xfers, 1);
    if (rc)
        return false;

    free(xfers[0].xd_targets->xt_objuuid);

    /* process the other xfer elements */
    rc = phobos_delete(xfers + 1, 2);
    if (rc)
        return false;

    free(xfers[1].xd_targets->xt_objuuid);
    free(xfers[2].xd_targets->xt_objuuid);

    return true;
}

static bool test_delete_failure(void)
{
    struct pho_xfer_target target = { .xt_objid = "not-an-object" };
    struct pho_xfer_desc xfer = { .xd_ntargets = 1, .xd_targets = &target };
    int rc;

    rc = phobos_delete(&xfer, 1);
    if (rc != -ENOENT)
        return false;

    return true;
}

static int check_copy_deleted(struct dss_handle *dss, const char *object_oid,
                              const char *object_uuid, const char *copy_name,
                              bool *deleted)
{
    struct dss_filter copy_filter;
    struct copy_info *copy_info;
    int copy_count;
    int rc;

    rc = dss_filter_build(&copy_filter,
                          "{\"$AND\": ["
                              "{\"DSS::COPY::object_uuid\": \"%s\"},"
                              "{\"DSS::COPY::copy_name\": \"%s\"}"
                          "]}", object_uuid, copy_name);
    if (rc) {
        pho_error(rc,
                  "Unable to build copy filter for object oid '%s', uuid '%s', "
                  "copy_name '%s'", object_oid, object_uuid, copy_name);
        return rc;
    }

    rc = dss_copy_get(dss, &copy_filter, &copy_info, &copy_count, NULL);
    dss_filter_free(&copy_filter);
    if (rc) {
        pho_error(rc,
                  "Unable to get copy for object oid '%s', uuid '%s', "
                  "copy_name '%s'", object_oid, object_uuid, copy_name);
        return rc;
    }

    dss_res_free(copy_info, copy_count);
    if (copy_count)
        *deleted = false;
    else
        *deleted = true;

    return 0;
}

static int check_object_deleted(struct dss_handle *dss, const char *oid,
                                const char *uuid, int version, bool *deleted)
{
    struct object_info *object_info;
    int rc;

    rc = dss_find_object(dss, oid, uuid, version, DSS_OBJ_ALL, &object_info);
    if (rc == -ENOENT) {
        *deleted = true;
        return 0;
    } else if (rc) {
        return rc;
    }

    object_info_free(object_info);
    *deleted = false;
    return 0;
}

static bool test_delete_incomplete_copy(void)
{
    struct dss_handle dss;
    bool deleted;
    int rc;

    rc = phobos_delete_incomplete_copy();
    if (rc)
        return false;

    /* check the copies and the objects are deleted */
    rc = dss_init(&dss);
    if (rc) {
        pho_error(rc, "Unable to init dss to check copies");
        return false;
    }

    rc = check_copy_deleted(&dss, "1_incomplete_copy",
                            "00112233445566778899aabbccddeef4", "source",
                            &deleted);
    if (rc || !deleted) {
        if (rc)
            pho_error(rc,
                      "Unable to checked the deleted copy 1_incomplete_copy");
        else
            pho_warn("1_incomplete_copy copy should be deleted");

        dss_fini(&dss);
        return false;
    }

    rc = check_object_deleted(&dss, "1_incomplete_copy",
                              "00112233445566778899aabbccddeef4", 1, &deleted);
    if (rc || !deleted) {
        if (rc)
            pho_error(rc,
                      "Unable to checked the deleted object 1_incomplete_copy");
        else
            pho_warn("1_incomplete_copy object should be deleted");

        dss_fini(&dss);
        return false;
    }

    rc = check_copy_deleted(&dss, "1_complete_copy_1_incomplete_copy",
                            "00112233445566778899aabbccddeef5", "source",
                            &deleted);
    if (rc || deleted) {
        if (rc)
            pho_error(rc,
                      "Unable to checked the 1_complete_copy_1_incomplete_copy "
                      "source copy");
        else
            pho_warn("1_complete_copy_1_incomplete_copy source copy should "
                     "not be deleted");

        dss_fini(&dss);
        return false;
    }

    rc = check_copy_deleted(&dss, "1_complete_copy_1_incomplete_copy",
                            "00112233445566778899aabbccddeef5", "incomplete",
                            &deleted);
    if (rc || !deleted) {
        if (rc)
            pho_error(rc,
                      "Unable to checked the 1_complete_copy_1_incomplete_copy "
                      "incomplete copy");
        else
            pho_warn("1_complete_copy_1_incomplete_copy incomplete copy "
                     "should be deleted");

        dss_fini(&dss);
        return false;
    }

    rc = check_object_deleted(&dss, "1_complete_copy_1_incomplete_copy",
                              "00112233445566778899aabbccddeef5", 1,
                              &deleted);
    if (rc || deleted) {
        if (rc)
            pho_error(rc,
                      "Unable to checked the 1_complete_copy_1_incomplete_copy "
                      "object");
        else
            pho_warn("1_complete_copy_1_incomplete_copy object should not be "
                     "deleted");

        dss_fini(&dss);
        return false;
    }

    rc = check_copy_deleted(&dss, "1_deprecated_incomplete_copy",
                            "00112233445566778899aabbccddeef6", "source",
                            &deleted);
    if (rc || !deleted) {
        if (rc)
            pho_error(rc,
                      "Unable to checked the 1_deprecated_incomplete_copy "
                      "source copy");
        else
            pho_warn("1_deprecated_incomplete_copy incomplete copy should be "
                     "deleted");

        dss_fini(&dss);
        return false;
    }

    rc = check_object_deleted(&dss, "1_deprecated_incomplete_copy",
                              "00112233445566778899aabbccddeef6", 1,
                              &deleted);
    if (rc || !deleted) {
        if (rc)
            pho_error(rc,
                      "Unable to checked the 1_deprecated_incomplete_copy "
                      "object");
        else
            pho_warn("1_deprecated_incomplete_copy object should be deleted");

        dss_fini(&dss);
        return false;
    }

    dss_fini(&dss);
    return true;
}

int main(void)
{
    bool (*test_function[])(void) = {
        test_delete_null_list,
        test_delete_success,
        test_delete_failure,
        test_delete_incomplete_copy,
    };
    struct dss_handle dss_handle;
    bool test_res = true;
    int rc;
    int i;

    test_env_initialize();

    rc = dss_init(&dss_handle);
    if (rc) {
        pho_error(rc, "dss_init failed");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < ARRAYSIZE(test_function); ++i) {
        pho_info("Test %d", i);
        test_res = !test_res ? test_res : test_function[i]();
    }

    dss_fini(&dss_handle);
    exit(test_res ? EXIT_SUCCESS : EXIT_FAILURE);
}
