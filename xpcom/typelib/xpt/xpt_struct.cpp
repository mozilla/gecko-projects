/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=4 et sw=4 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Implementation of XDR routines for typelib structures. */

#include "xpt_xdr.h"
#include "xpt_struct.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

using mozilla::WrapNotNull;

#define XPT_MAGIC "XPCOM\nTypeLib\r\n\032"
#define XPT_MAGIC_STRING "XPCOM\\nTypeLib\\r\\n\\032"

/*
 * Annotation records are variable-size records used to store secondary
 * information about the typelib, e.g. such as the name of the tool that
 * generated the typelib file, the date it was generated, etc.  The
 * information is stored with very loose format requirements so as to
 * allow virtually any private data to be stored in the typelib.
 *
 * There are two types of Annotations:
 *
 * EmptyAnnotation
 * PrivateAnnotation
 *
 * The tag field of the prefix discriminates among the variant record
 * types for Annotation's.  If the tag is 0, this record is an
 * EmptyAnnotation. EmptyAnnotation's are ignored - they're only used to
 * indicate an array of Annotation's that's completely empty.  If the tag
 * is 1, the record is a PrivateAnnotation.
 *
 * We don't actually store annotations; we just skip over them if they are
 * present.
 */

#define XPT_ANN_LAST    0x80
#define XPT_ANN_PRIVATE 0x40

#define XPT_ANN_IS_LAST(flags) (flags & XPT_ANN_LAST)
#define XPT_ANN_IS_PRIVATE(flags)(flags & XPT_ANN_PRIVATE)


/***************************************************************************/
/* Forward declarations. */

static bool
DoInterfaceDirectoryEntry(XPTArena *arena, NotNull<XPTCursor*> cursor,
                          XPTInterfaceDirectoryEntry *ide);

static bool
DoConstDescriptor(XPTArena *arena, NotNull<XPTCursor*> cursor,
                  XPTConstDescriptor *cd, XPTInterfaceDescriptor *id);

static bool
DoMethodDescriptor(XPTArena *arena, NotNull<XPTCursor*> cursor,
                   XPTMethodDescriptor *md, XPTInterfaceDescriptor *id);

static bool
SkipAnnotation(NotNull<XPTCursor*> cursor, bool *isLast);

static bool
DoInterfaceDescriptor(XPTArena *arena, NotNull<XPTCursor*> outer,
                      XPTInterfaceDescriptor **idp);

static bool
DoTypeDescriptorPrefix(XPTArena *arena, NotNull<XPTCursor*> cursor,
                       XPTTypeDescriptorPrefix *tdp);

static bool
DoTypeDescriptor(XPTArena *arena, NotNull<XPTCursor*> cursor,
                 XPTTypeDescriptor *td, XPTInterfaceDescriptor *id);

static bool
DoParamDescriptor(XPTArena *arena, NotNull<XPTCursor*> cursor,
                  XPTParamDescriptor *pd, XPTInterfaceDescriptor *id);

/***************************************************************************/

bool
XPT_DoHeader(XPTArena *arena, NotNull<XPTCursor*> cursor, XPTHeader **headerp)
{
    unsigned int i;
    uint32_t file_length = 0;
    uint32_t ide_offset;

    XPTHeader* header = XPT_NEWZAP(arena, XPTHeader);
    if (!header)
        return false;
    *headerp = header;

    uint8_t magic[16];
    for (i = 0; i < sizeof(magic); i++) {
        if (!XPT_Do8(cursor, &magic[i]))
            return false;
    }

    if (strncmp((const char*)magic, XPT_MAGIC, 16) != 0) {
        /* Require that the header contain the proper magic */
        fprintf(stderr,
                "libxpt: bad magic header in input file; "
                "found '%s', expected '%s'\n",
                magic, XPT_MAGIC_STRING);
        return false;
    }

    uint8_t minor_version;
    if (!XPT_Do8(cursor, &header->major_version) ||
        !XPT_Do8(cursor, &minor_version)) {
        return false;
    }

    if (header->major_version >= XPT_MAJOR_INCOMPATIBLE_VERSION) {
        /* This file is newer than we are and set to an incompatible version
         * number. We must set the header state thusly and return.
         */
        header->num_interfaces = 0;
        return true;
    }

    if (!XPT_Do16(cursor, &header->num_interfaces) ||
        !XPT_Do32(cursor, &file_length) ||
        !XPT_Do32(cursor, &ide_offset)) {
        return false;
    }

    /*
     * Make sure the file length reported in the header is the same size as
     * as our buffer unless it is zero (not set)
     */
    if (file_length != 0 &&
        cursor->state->pool_allocated < file_length) {
        fputs("libxpt: File length in header does not match actual length. File may be corrupt\n",
            stderr);
        return false;
    }

    uint32_t data_pool;
    if (!XPT_Do32(cursor, &data_pool))
        return false;

    XPT_SetDataOffset(cursor->state, data_pool);

    if (header->num_interfaces) {
        size_t n = header->num_interfaces * sizeof(XPTInterfaceDirectoryEntry);
        header->interface_directory =
            static_cast<XPTInterfaceDirectoryEntry*>(XPT_CALLOC8(arena, n));
        if (!header->interface_directory)
            return false;
    }

    /*
     * Iterate through the annotations rather than recurring, to avoid blowing
     * the stack on large xpt files. We don't actually store annotations, we
     * just skip over them.
     */
    bool isLast;
    do {
        if (!SkipAnnotation(cursor, &isLast))
            return false;
    } while (!isLast);

    /* shouldn't be necessary now, but maybe later */
    XPT_SeekTo(cursor, ide_offset);

    for (i = 0; i < header->num_interfaces; i++) {
        if (!DoInterfaceDirectoryEntry(arena, cursor,
                                       &header->interface_directory[i]))
            return false;
    }

    return true;
}

/* InterfaceDirectoryEntry records go in the header */
bool
DoInterfaceDirectoryEntry(XPTArena *arena, NotNull<XPTCursor*> cursor,
                          XPTInterfaceDirectoryEntry *ide)
{
    char* dummy_name_space;

    /* write the IID in our cursor space */
    if (!XPT_DoIID(cursor, &(ide->iid)) ||

        /* write the name string in the data pool, and the offset in our
           cursor space */
        !XPT_DoCString(arena, cursor, &(ide->name)) ||

        /* don't write the name_space string in the data pool, because we don't
         * need it. Do write the offset in our cursor space */
        !XPT_DoCString(arena, cursor, &dummy_name_space, /* ignore = */ true) ||

        /* do InterfaceDescriptors */
        !DoInterfaceDescriptor(arena, cursor, &ide->interface_descriptor)) {
        return false;
    }

    return true;
}

static bool
InterfaceDescriptorAddType(XPTArena *arena, XPTInterfaceDescriptor *id)
{
    XPTTypeDescriptor *old = id->additional_types;
    XPTTypeDescriptor *new_;
    size_t old_size = id->num_additional_types * sizeof(XPTTypeDescriptor);
    size_t new_size = old_size + sizeof(XPTTypeDescriptor);

    /* XXX should grow in chunks to minimize alloc overhead */
    new_ = static_cast<XPTTypeDescriptor*>(XPT_CALLOC8(arena, new_size));
    if (!new_)
        return false;
    if (old) {
        memcpy(new_, old, old_size);
    }
    id->additional_types = new_;

    if (id->num_additional_types == UINT8_MAX)
        return false;

    id->num_additional_types += 1;
    return true;
}

bool
DoInterfaceDescriptor(XPTArena *arena, NotNull<XPTCursor*> outer,
                      XPTInterfaceDescriptor **idp)
{
    XPTInterfaceDescriptor *id;
    XPTCursor curs;
    NotNull<XPTCursor*> cursor = WrapNotNull(&curs);
    uint32_t i, id_sz = 0;

    id = XPT_NEWZAP(arena, XPTInterfaceDescriptor);
    if (!id)
        return false;
    *idp = id;

    if (!XPT_MakeCursor(outer->state, XPT_DATA, id_sz, cursor))
        return false;

    if (!XPT_Do32(outer, &cursor->offset))
        return false;
    if (!cursor->offset) {
        *idp = NULL;
        return true;
    }
    if(!XPT_Do16(cursor, &id->parent_interface) ||
       !XPT_Do16(cursor, &id->num_methods)) {
        return false;
    }

    if (id->num_methods) {
        size_t n = id->num_methods * sizeof(XPTMethodDescriptor);
        id->method_descriptors =
            static_cast<XPTMethodDescriptor*>(XPT_CALLOC8(arena, n));
        if (!id->method_descriptors)
            return false;
    }

    for (i = 0; i < id->num_methods; i++) {
        if (!DoMethodDescriptor(arena, cursor, &id->method_descriptors[i], id))
            return false;
    }

    if (!XPT_Do16(cursor, &id->num_constants)) {
        return false;
    }

    if (id->num_constants) {
        size_t n = id->num_constants * sizeof(XPTConstDescriptor);
        id->const_descriptors =
            static_cast<XPTConstDescriptor*>(XPT_CALLOC8(arena, n));
        if (!id->const_descriptors)
            return false;
    }

    for (i = 0; i < id->num_constants; i++) {
        if (!DoConstDescriptor(arena, cursor, &id->const_descriptors[i], id)) {
            return false;
        }
    }

    if (!XPT_Do8(cursor, &id->flags)) {
        return false;
    }

    return true;
}

bool
DoConstDescriptor(XPTArena *arena, NotNull<XPTCursor*> cursor,
                  XPTConstDescriptor *cd, XPTInterfaceDescriptor *id)
{
    bool ok = false;

    if (!XPT_DoCString(arena, cursor, &cd->name) ||
        !DoTypeDescriptor(arena, cursor, &cd->type, id)) {

        return false;
    }

    switch (cd->type.Tag()) {
      case TD_INT16:
        ok = XPT_Do16(cursor, (uint16_t*) &cd->value.i16);
        break;
      case TD_INT32:
        ok = XPT_Do32(cursor, (uint32_t*) &cd->value.i32);
        break;
      case TD_UINT16:
        ok = XPT_Do16(cursor, &cd->value.ui16);
        break;
      case TD_UINT32:
        ok = XPT_Do32(cursor, &cd->value.ui32);
        break;
      default:
        MOZ_ASSERT(false, "illegal type");
        break;
    }

    return ok;
}

bool
DoMethodDescriptor(XPTArena *arena, NotNull<XPTCursor*> cursor,
                   XPTMethodDescriptor *md, XPTInterfaceDescriptor *id)
{
    int i;

    if (!XPT_Do8(cursor, &md->flags) ||
        !XPT_DoCString(arena, cursor, &md->name) ||
        !XPT_Do8(cursor, &md->num_args))
        return false;

    if (md->num_args) {
        size_t n = md->num_args * sizeof(XPTParamDescriptor);
        md->params = static_cast<XPTParamDescriptor*>(XPT_CALLOC8(arena, n));
        if (!md->params)
            return false;
    }

    for(i = 0; i < md->num_args; i++) {
        if (!DoParamDescriptor(arena, cursor, &md->params[i], id))
            return false;
    }

    // |result| appears in the on-disk format but it isn't used,
    // because a method is either notxpcom, in which case it can't be
    // called from script so the XPT information is irrelevant, or the
    // result type is nsresult.
    XPTParamDescriptor result;
    if (!DoParamDescriptor(arena, cursor, &result, id))
        return false;

    return true;
}

bool
DoParamDescriptor(XPTArena *arena, NotNull<XPTCursor*> cursor,
                  XPTParamDescriptor *pd, XPTInterfaceDescriptor *id)
{
    if (!XPT_Do8(cursor, &pd->flags) ||
        !DoTypeDescriptor(arena, cursor, &pd->type, id))
        return false;

    return true;
}

bool
DoTypeDescriptorPrefix(XPTArena *arena, NotNull<XPTCursor*> cursor,
                       XPTTypeDescriptorPrefix *tdp)
{
    return XPT_Do8(cursor, &tdp->flags);
}

bool
DoTypeDescriptor(XPTArena *arena, NotNull<XPTCursor*> cursor,
                 XPTTypeDescriptor *td, XPTInterfaceDescriptor *id)
{
    if (!DoTypeDescriptorPrefix(arena, cursor, &td->prefix)) {
        return false;
    }

    switch (td->Tag()) {
      case TD_INTERFACE_TYPE:
        uint16_t iface;
        if (!XPT_Do16(cursor, &iface))
            return false;
        td->u.iface.iface_hi8 = (iface >> 8) & 0xff;
        td->u.iface.iface_lo8 = iface & 0xff;
        break;
      case TD_INTERFACE_IS_TYPE:
        if (!XPT_Do8(cursor, &td->u.interface_is.argnum))
            return false;
        break;
      case TD_ARRAY: {
        // argnum2 appears in the on-disk format but it isn't used.
        uint8_t argnum2 = 0;
        if (!XPT_Do8(cursor, &td->u.array.argnum) ||
            !XPT_Do8(cursor, &argnum2))
            return false;

        if (!InterfaceDescriptorAddType(arena, id))
            return false;
        td->u.array.additional_type = id->num_additional_types - 1;

        if (!DoTypeDescriptor(arena, cursor,
                              &id->additional_types[td->u.array.additional_type],
                              id))
            return false;
        break;
      }
      case TD_PSTRING_SIZE_IS:
      case TD_PWSTRING_SIZE_IS: {
        // argnum2 appears in the on-disk format but it isn't used.
        uint8_t argnum2 = 0;
        if (!XPT_Do8(cursor, &td->u.pstring_is.argnum) ||
            !XPT_Do8(cursor, &argnum2))
            return false;
        break;
      }
      default:
        /* nothing special */
        break;
    }
    return true;
}

bool
SkipAnnotation(NotNull<XPTCursor*> cursor, bool *isLast)
{
    uint8_t flags;
    if (!XPT_Do8(cursor, &flags))
        return false;

    *isLast = XPT_ANN_IS_LAST(flags);

    if (XPT_ANN_IS_PRIVATE(flags)) {
        if (!XPT_SkipStringInline(cursor) ||
            !XPT_SkipStringInline(cursor))
            return false;
    }

    return true;
}

