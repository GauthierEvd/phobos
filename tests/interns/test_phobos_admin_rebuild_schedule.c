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
 * \brief  Tests for phobos_admin_media_rebuild scheduler
 */

/* phobos stuff */
#include "rebuild.h"
#include "phobos_admin.h"
#include "test_setup.h"

/* standard stuff */

/* cmocka stuff */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static void create_pho_id(struct pho_id *id, const char *name)
{
    memset(id, 0, sizeof(*id));

    id->family = PHO_RSC_TAPE;
    pho_id_name_set(id, name, "legacy");
}

static int pho_id_cmp(const void *a, const void *b)
{
    const struct pho_id *id_a = a;
    const struct pho_id *id_b = b;

    return strcmp(id_a->name, id_b->name);
}

static struct group_key *create_group_key_from_media(struct pho_id **media,
                                                     int n_media)
{
    struct group_key *key;

    key = xmalloc(sizeof(*key) + n_media * sizeof(*key->media));
    key->n_media = n_media;

    for (int i = 0; i < n_media; i++)
        pho_id_copy(&key->media[i], media[i]);

    qsort(key->media, key->n_media, sizeof(*key->media), pho_id_cmp);

    return key;
}

static struct group_key *create_group_key(struct pho_id *med1,
                                          struct pho_id *med2)
{
    struct pho_id *media[] = {med1, med2};

    return create_group_key_from_media(media, 2);
}

static struct group_key *create_single_group_key(struct pho_id *med)
{
    struct pho_id *media[] = {med};

    return create_group_key_from_media(media, 1);
}

static GPtrArray *create_group_items(int weight)
{
    GPtrArray *items = g_ptr_array_new();

    for (int i = 0; i < weight; i++)
        g_ptr_array_add(items, NULL);

    return items;
}

static GPtrArray *assert_next_group(struct rebuild_scheduler *sched,
                                    const char *med1, const char *med2)
{
    GPtrArray *items = NULL;
    int n_media = med2 == NULL ? 1 : 2;

    assert_true(rebuild_scheduler_next(sched, &items));
    assert_non_null(items);
    assert_string_equal(sched->curr_media->media[0].name, med1);
    if (med2 != NULL)
        assert_string_equal(sched->curr_media->media[1].name, med2);

    assert_int_equal(sched->curr_media->n_media, n_media);

    return items;
}

static void test_basic_schedule(void **state)
{
    struct rebuild_scheduler *sched = rebuild_scheduler_new();
    GPtrArray *items = NULL;
    struct pho_id media[5];

    create_pho_id(&media[0], "A");
    create_pho_id(&media[1], "B");
    create_pho_id(&media[2], "C");
    create_pho_id(&media[3], "D");
    create_pho_id(&media[4], "E");

    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&media[0], &media[1]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&media[3], &media[4]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&media[1], &media[2]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&media[2], &media[3]),
                        create_group_items(0));

    assert_next_group(sched, "D", "E");
    assert_next_group(sched, "C", "D");
    assert_next_group(sched, "B", "C");
    assert_next_group(sched, "A", "B");
    assert_false(rebuild_scheduler_next(sched, &items));

    rebuild_scheduler_free(sched);
}

static void test_schedule_with_singletons(void **state)
{
    struct rebuild_scheduler *sched = rebuild_scheduler_new();
    GPtrArray *items = NULL;
    struct pho_id med[4];

    create_pho_id(&med[0], "A");
    create_pho_id(&med[1], "B");
    create_pho_id(&med[2], "C");
    create_pho_id(&med[3], "D");

    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[2], &med[3]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_single_group_key(&med[2]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_single_group_key(&med[1]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[1], &med[2]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[0], &med[1]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_single_group_key(&med[0]),
                        create_group_items(0));

    assert_next_group(sched, "A", NULL);
    assert_next_group(sched, "A", "B");
    assert_next_group(sched, "B", NULL);
    assert_next_group(sched, "B", "C");
    assert_next_group(sched, "C", NULL);
    assert_next_group(sched, "C", "D");
    assert_false(rebuild_scheduler_next(sched, &items));

    rebuild_scheduler_free(sched);
}

static void test_with_two_separate_components(void **state)
{
    struct rebuild_scheduler *sched = rebuild_scheduler_new();
    GPtrArray *items = NULL;
    struct pho_id med[7];

    create_pho_id(&med[0], "A");
    create_pho_id(&med[1], "B");
    create_pho_id(&med[2], "C");
    create_pho_id(&med[3], "D");
    create_pho_id(&med[4], "X");
    create_pho_id(&med[5], "Y");
    create_pho_id(&med[6], "Z");

    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[5], &med[6]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[4], &med[5]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[2], &med[3]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[1], &med[2]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[0], &med[1]),
                        create_group_items(0));

    assert_next_group(sched, "A", "B");
    assert_next_group(sched, "B", "C");
    assert_next_group(sched, "C", "D");
    assert_next_group(sched, "X", "Y");
    assert_next_group(sched, "Y", "Z");
    assert_false(rebuild_scheduler_next(sched, &items));

    rebuild_scheduler_free(sched);
}

static void test_prefers_lower_overlap(void **state)
{
    struct rebuild_scheduler *sched = rebuild_scheduler_new();
    GPtrArray *items = NULL;
    struct pho_id med[4];

    create_pho_id(&med[0], "A");
    create_pho_id(&med[1], "B");
    create_pho_id(&med[2], "C");
    create_pho_id(&med[3], "D");

    g_hash_table_insert(sched->pending_groups,
                        create_single_group_key(&med[0]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[0], &med[1]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[1], &med[2]),
                        create_group_items(0));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[1], &med[3]),
                        create_group_items(1));

    assert_next_group(sched, "A", NULL);
    assert_next_group(sched, "A", "B");
    assert_next_group(sched, "B", "D");
    assert_next_group(sched, "B", "C");

    assert_false(rebuild_scheduler_next(sched, &items));

    rebuild_scheduler_free(sched);
}

static void test_weight_breaks_group_tie(void **state)
{
    struct rebuild_scheduler *sched = rebuild_scheduler_new();
    GPtrArray *items = NULL;
    struct pho_id med[3];

    create_pho_id(&med[0], "A");
    create_pho_id(&med[1], "B");
    create_pho_id(&med[2], "C");

    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[0], &med[2]),
                        create_group_items(2));
    g_hash_table_insert(sched->pending_groups,
                        create_group_key(&med[0], &med[1]),
                        create_group_items(1));
    g_hash_table_insert(sched->pending_groups,
                        create_single_group_key(&med[0]),
                        create_group_items(3));

    items = assert_next_group(sched, "A", NULL);
    assert_int_equal(items->len, 3);
    items = assert_next_group(sched, "A", "C");
    assert_int_equal(items->len, 2);
    items = assert_next_group(sched, "A", "B");
    assert_int_equal(items->len, 1);
    assert_false(rebuild_scheduler_next(sched, &items));

    rebuild_scheduler_free(sched);
}

int main(void)
{
    const struct CMUnitTest phobos_admin_media_rebuild_schedule[] = {
        cmocka_unit_test(test_basic_schedule),
        cmocka_unit_test(test_schedule_with_singletons),
        cmocka_unit_test(test_with_two_separate_components),
        cmocka_unit_test(test_prefers_lower_overlap),
        cmocka_unit_test(test_weight_breaks_group_tie),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(phobos_admin_media_rebuild_schedule,
                                  global_setup_admin_no_lrs_with_dbinit,
                                  global_teardown_admin_with_dbdrop);
}
