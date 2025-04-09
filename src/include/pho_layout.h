/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Phobos Data Layout management.
 */
#ifndef _PHO_LAYOUT_H
#define _PHO_LAYOUT_H

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

#include "phobos_store.h"
#include "pho_dss.h"
#include "pho_srl_lrs.h"
#include "pho_io.h"

/**
 * Operation names for dynamic loading with dlsym()
 * This is the publicly exposed API that layout modules provide.
 *
 * See below for the corresponding prototypes and additional information.
 */
#define PLM_OP_INIT         "pho_layout_mod_register"

struct pho_io_descr;
struct layout_info;

struct pho_data_processor;

/**
 * Operation provided by a layout module.
 *
 * See layout_encode, layout_decode and layout_erase for a more complete
 * documentation.
 */
struct pho_layout_module_ops {
    /** Initialize a new encoder to put an object into phobos */
    int (*encode)(struct pho_data_processor *encoder);

    /** Initialize a new decoder to get an object from phobos */
    int (*decode)(struct pho_data_processor *decoder);

    /** Initialize a new eraser to delete an object from phobos */
    int (*erase)(struct pho_data_processor *eraser);

    /** Retrieve one node name from which an object can be accessed */
    int (*locate)(struct dss_handle *dss, struct layout_info *layout,
                  const char *focus_host, char **hostname, int *nb_new_lock);

    /** Updates the information of the layout, object and extent based on the
     * medium's extent and the layout used.
     */
    int (*get_specific_attrs)(struct pho_io_descr *iod,
                              struct io_adapter_module *ioa,
                              struct extent *extent,
                              struct pho_attrs *layout_md);

    /** Updates the status of an copy based on its extents */
    int (*reconstruct)(struct layout_info lyt, struct copy_info *copy);
};

/** Operations provided by a given data processor.
 *
 * The processors communicate their needs to the LRS via requests (see
 * pho_lrs.h) and retrieve corresponding responses, allowing them to eventually
 * perform the required IOs.
 *
 * See layout_next_request, layout_receive_response and layout_destroy for a
 * more complete documentation.
 */
struct pho_proc_ops {
    /** Give a response and get requests from this encoder / decoder */
    int (*step)(struct pho_data_processor *proc, pho_resp_t *resp, pho_req_t
                **reqs, size_t *n_reqs);

    /** Destroy this encoder / decoder */
    void (*destroy)(struct pho_data_processor *proc);
};

/**
 * A layout module, implementing one way of encoding a file into a phobos
 * object (simple, raid1, compression, etc.).
 *
 * Each layout module fills this structure in its entry point (PLM_OP_INIT).
 */
struct layout_module {
    void *dl_handle;            /**< Handle to the layout plugin */
    struct module_desc desc;    /**< Description of this layout */
    const struct pho_layout_module_ops *ops; /**< Operations of this layout */
};

/**
 * The different types of data processors
 */
enum processor_type {
    PHO_PROC_ENCODER,
    PHO_PROC_DECODER,
    PHO_PROC_ERASER,
};


/** A data processor capable of encoding, decoding or erasing  one object on
 * a set of media
 */
struct pho_data_processor {
    enum processor_type type;       /**< Type of the data processor */
    void *private_processor;        /**< Layouts specific data */
    const struct pho_proc_ops *ops; /**< Layouts specific operations */
    bool done;                      /**< True if this data processor has no more
                                      *  work to do (check rc to know if an
                                      *  error happened)
                                      */
    struct pho_xfer_desc *xfer;     /**< Transfer descriptor (managed
                                      *  externally)
                                      */
    struct layout_info *layout;     /**< Layouts of the current transfer filled
                                      *  out when decoding
                                      */
    size_t io_block_size;           /**< Block size (in bytes) of the I/O buffer
                                      */
    pho_resp_t *last_resp;          /**< Last response from the LRS (use for
                                      *  a mput with no-split to keep the write
                                      *  resp)
                                      */
    /*
     * XXX BEGIN UNDER CONSTRUCTION SECTION
     */
    int current_target;
    size_t object_size;
    size_t reader_offset;
    size_t reader_stripe_size;
    size_t writer_offset;
    size_t writer_stripe_size;
    /*
     * buff size is the lowest common multiple of the reader and the writer
     * stripe size.
     * buff is filled with (reader_offset - writer_offset) bytes.
     */
    struct pho_buff buff;
    void *private_reader;
    const struct pho_proc_ops *reader_ops;
    void *private_writer;
    const struct pho_proc_ops *writer_ops;
};

/**
 * The data processor reader put data into the data processor buffer.
 *
 * \a proc buff is filled and its reader offset is updated.
 *
 * @param[in,out] proc          Data processor
 * @param[in,out] reader_iod    IO descriptor given by the reader
 * @param[in]     size          Number of bytes to read
 *
 * @return 0 on success, -errno on error.
 */
int data_processor_read_into_buff(struct pho_data_processor *proc,
                                  struct pho_io_descr *reader_iod, size_t size);

/**
 * Check if the data processor is of type encoder.
 */
static inline bool is_encoder(struct pho_data_processor *processor)
{
    return processor->type == PHO_PROC_ENCODER;
}

/**
 * Check if the data processor is of type decoder.
 */
static inline bool is_decoder(struct pho_data_processor *processor)
{
    return processor->type == PHO_PROC_DECODER;
}

/**
 * Check if the data processor is of type eraser.
 */
static inline bool is_eraser(struct pho_data_processor *processor)
{
    return processor->type == PHO_PROC_ERASER;
}

static inline const char *processor_type2str(struct pho_data_processor *proc)
{
    switch (proc->type) {
    case PHO_PROC_ENCODER:
        return "encoder";
    case PHO_PROC_DECODER:
        return "decoder";
    case PHO_PROC_ERASER:
        return "eraser";
    default:
        return "unknow";
    }
}

/**
 * @defgroup pho_layout_mod Public API for layout modules
 * @{
 */

/**
 * Not for direct call.
 * Entry point of layout modules.
 *
 * The function fills the module description (.desc) and operation (.ops) fields
 * for this specific layout module.
 *
 * Global initialization operations can be performed here if need be.
 */
int pho_layout_mod_register(struct layout_module *self);

/** @} end of pho_layout_mod group */

#define CHECK_ENC_OP(_enc, _func) do { \
    assert(_enc); \
    assert(_enc->ops); \
    assert(_enc->ops->_func); \
} while (0)

/**
 * Initialize a new data processor \a enc to put an object described by \a xfer
 * in phobos.
 *
 * @param[out]  encoder Encoder to be initialized
 * @param[in]   xfer    Transfer to make this encoder work on. A reference on it
 *                      will be kept as long as \a enc is in use and some of its
 *                      fields may be modified (notably xd_rc). A xfer may only
 *                      be handled by one encoder.
 *
 * @return 0 on success, -errno on error.
 */
int layout_encoder(struct pho_data_processor *encoder,
                   struct pho_xfer_desc *xfer);

/**
 * Initialize a new data processor \a dec to get an object described by \a xfer
 * from phobos.
 *
 * @param[out]  decoder Decoder to be initialized
 * @param[in]   xfer    Transfer to make this encoder work on. A reference on it
 *                      will be kept as long as \a enc is in use and some of its
 *                      fields may be modified (notably xd_rc).
 * @param[in]   layout  Layout of the object to retrieve. It is used in-place by
 *                      the decoder and must be freed separately by the caller
 *                      after the decoder is destroyed.
 *
 * @return 0 on success, -errno on error.
 */
int layout_decoder(struct pho_data_processor *decoder,
                   struct pho_xfer_desc *xfer, struct layout_info *layout);

/**
 * Initialize a new data processor \a dec to delete an object given by its ID
 * \a oid.
 *
 * @param[out]  eraser  Eraser to be initialized
 * @param[in]   xfer    Information of the object requested to be deleted
 * @param[in]   layout  Layout of the object to delete. It is used in-place by
 *                      the eraser and must be freed separately by the caller
 *                      after the encoder is destroyed
 *
 * @return 0 on success, -errno on error
 */
int layout_eraser(struct pho_data_processor *eraser,
                  struct pho_xfer_desc *xfer, struct layout_info *layout);

/**
 * Retrieve one node name from which an object can be accessed.
 *
 * @param[in]   dss         DSS handle
 * @param[in]   layout      Layout of the object to locate
 * @param[in]   focus_host  Hostname on which the caller would like to access
 *                          the object if there is no more convenient node (if
 *                          NULL, focus_host is set to local hostname)
 * @param[out]  hostname    Allocated and returned hostname of the node that
 *                          gives access to the object (NULL if an error
 *                          occurs)
 * @param[out]  nb_new_lock Number of new locks on media added for the returned
 *                          hostname
 *
 * @return                  0 on success or -errno on failure.
 *                          -ENODEV if there is no existing medium to retrieve
 *                          this layout
 *                          -EINVAL on invalid replica count or invalid layout
 *                          name
 *                          -EAGAIN if there is currently no convenient node to
 *                          retrieve this layout
 *                          -EADDRNOTAVAIL if we cannot get self hostname
 */
int layout_locate(struct dss_handle *dss, struct layout_info *layout,
                  const char *focus_host, char **hostname, int *nb_new_lock);

/**
 * Advance the layout operation of one step by providing a response from the LRS
 * (or NULL for the first call to this function) and collecting newly emitted
 * requests.
 *
 * @param[in]   proc    The data processor to advance.
 * @param[in]   resp    The response to pass to the data processor (or NULL for
 *                      the first call to this function).
 * @param[out]  reqs    Caller allocated array of newly emitted requests (to be
 *                      freed by the caller).
 * @param[out]  n_reqs  Number of emitted requests.
 *
 * @return 0 on success, -errno on error. -EINVAL is returned when the data
 * data processor has already finished its work (the call to this function was
 * unexpected).
 */
static inline int layout_step(struct pho_data_processor *proc, pho_resp_t *resp,
                              pho_req_t **reqs, size_t *n_reqs)
{
    if (proc->done)
        return -EINVAL;

    CHECK_ENC_OP(proc, step);
    return proc->ops->step(proc, resp, reqs, n_reqs);
}

/**
 * Update extent and layout metadata without attributes retrieved from the
 * extent using the io adapter provided.
 *
 * @param[in]      iod         The I/O descriptor of the entry
 * @param[in]      ioa         I/O adapter to use to get metadata.
 * @param[out]     ext         The extent to update.
 * @param[in,out]  lyt         The layout to retrieve the info from, and the
 *                             attributes to update.
 *
 * @return 0 on success, -errno on failure.
 */
int layout_get_specific_attrs(struct pho_io_descr *iod,
                              struct io_adapter_module *ioa,
                              struct extent *extent,
                              struct layout_info *layout);

/**
 * Updates the status of the object according to its detected extents
 *
 * @param[in]   lyt     The layout containing the extents.
 * @param[out]  copy    The copy to update.
 *
 * @return 0 on success, -errno on error.
 */
int layout_reconstruct(struct layout_info lyt, struct copy_info *copy);

/**
 * Destroy this data processor and all associated resources.
 */
void layout_destroy(struct pho_data_processor *proc);

#endif
